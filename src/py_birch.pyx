# distutils: language = c++
# distutils: sources = birch_util.cpp Fp.cpp Isometry.cpp Math.cpp QuadForm.cpp
# distutils: extra_compile_args = -g -Wall -Werror -pedantic -std=c++11 -fvar-tracking-assignments-toggle

import logging
from datetime import datetime
import numpy as np
from scipy.sparse import csr_matrix

from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.map cimport map as cppmap
from libcpp.memory cimport shared_ptr, make_shared
from libc.stdint cimport uint64_t
from libc.stdint cimport int64_t as Z64
from libc.stdint cimport uint16_t as W16
from libc.stdint cimport uint32_t as W32

from operator import itemgetter
from random import randint

from cython.operator import dereference as deref
from cython.operator import postincrement as incr

from sage.rings.integer cimport Integer
from sage.libs.gmp.types cimport mpz_t

from sage.all import QuadraticForm
from sage.all import Integers, Rationals
from sage.all import random_prime
from sage.all import matrix

from math import log, exp, lgamma

cdef extern from "<utility>" namespace "std" nogil:
    T move[T](T)

cdef extern from "gmpxx.h":
    cdef cppclass mpz_class:
        mpz_class(mpz_t a)
        string get_str(int base=10)

cdef extern from "QuadForm.h":
    cdef cppclass PrimeSymbol[R]:
        R p
        int power
        int ramified

    cdef cppclass QuadForm[R]:
        QuadForm()
        const R& a() const
        const R& b() const
        const R& c() const
        const R& f() const
        const R& g() const
        const R& h() const

        @staticmethod
        QuadForm[R] get_quad_form(const vector[PrimeSymbol[R]]& primes) except +

cdef extern from "Genus.h":
    cdef cppclass Genus[R]:
        Genus()
        Genus(const QuadForm[R]& q, const vector[PrimeSymbol[R]]& symbols, uint64_t seed)
        cppmap[R,size_t] dimension_map() const
        uint64_t seed() const
        cppmap[R,vector[int]] hecke_matrix_dense(const R& p) except +
        cppmap[R,vector[vector[int]]] hecke_matrix_sparse(const R& p) except +

        @staticmethod
        Genus[T] convert[T](const Genus[R]& src)

cdef extern from "Isometry.h":
    cdef cppclass Isometry[R]:
        R a11
        R a12
        R a13
        R a21
        R a22
        R a23
        R a31
        R a32
        R a33
        pass

cdef extern from "IsometrySequence.h":
    cdef cppclass IsometrySequenceData[T]:
        Isometry[T] isometry
        T denominator
        size_t src
        size_t dst
    cdef cppclass IsometrySequence[R,S,T]:
        IsometrySequence(shared_ptr[Genus[T]], const T& p)
        int done() const
        IsometrySequenceData next()

ctypedef mpz_class Z
ctypedef PrimeSymbol[Z] Z_PrimeSymbol
ctypedef QuadForm[Z] Z_QuadForm

cdef class BirchGenus:
    cdef shared_ptr[Genus[Z]] Z_genus
    cdef shared_ptr[Genus[Z64]] Z64_genus
    cpdef Z64_genus_is_set
    cpdef N
    cpdef ramified_primes
    cpdef facs
    cpdef dims
    cpdef seed_
    cpdef hecke

    def __init__(self, N, ramified_primes=None, seed=None):
        self.N = Integer(N)
        self.facs = self.N.factor()
        ps = map(itemgetter(0), self.facs)
        es = map(itemgetter(1), self.facs)

        if ramified_primes is None:
            logging.info("Ramified primes: chosen automatically")
            if len(ps) % 2 == 0:
                self.ramified_primes = ps[:-1]
            else:
                self.ramified_primes = ps[:]
        else:
            logging.info("Ramified primes: specified by user")
            self.ramified_primes = [ p for p in ramified_primes if p in ps ]

        cdef vector[Z_PrimeSymbol] primes
        cdef Z_PrimeSymbol prime
        for n,p in enumerate(ps):
            prime.p = Z(Integer(p).value)
            prime.power = int(es[n])
            prime.ramified = p in self.ramified_primes
            primes.push_back(prime)
            logging.info("{} at {}".format("Ramified" if prime.ramified else "Unramified", p))

        cdef Z_QuadForm q
        try:
            logging.info("Determining desired quadratic form")
            q = Z_QuadForm.get_quad_form(primes)
            a = _Z_to_int(q.a())
            b = _Z_to_int(q.b())
            c = _Z_to_int(q.c())
            f = _Z_to_int(q.f())
            g = _Z_to_int(q.g())
            h = _Z_to_int(q.h())
            S = "Chose Q(x,y,z) = "
            if a:
                S += "{}{}x^2".format("" if a > 0 else "-", abs(a) if abs(a) != 1 else "")
            if b:
                S += " {} {}y^2".format("+" if b > 0 else "-", abs(b) if abs(b) != 1 else "")
            if c:
                S += " {} {}z^2".format("+" if c > 0 else "-", abs(c) if abs(c) != 1 else "")
            if f:
                S += " {} {}yz".format("+" if f > 0 else "-", abs(f) if abs(f) != 1 else "")
            if g:
                S += " {} {}xz".format("+" if g > 0 else "-", abs(g) if abs(g) != 1 else "")
            if h:
                S += " {} {}xy".format("+" if h > 0 else "-", abs(h) if abs(h) != 1 else "")
            logging.info(S)
        except Exception as e:
            raise Exception(e.message)

        seed = seed if seed else 0
        cdef uint64_t arg_seed = seed

        logging.info("Computing genus representatives...")
        genus_start = datetime.now()
        self.Z_genus = make_shared[Genus[Z]](q, primes, arg_seed)
        genus_stop = datetime.now()
        logging.info("Finished computing genus representatives (time: {})".format(genus_stop-genus_start))
        self.seed_ = deref(self.Z_genus).seed()
        logging.info("Seed = {} ({})".format(self.seed_, "provided by user" if seed else "set randomly"))

        cdef cppmap[Z,size_t] mymap = deref(self.Z_genus).dimension_map()
        cdef cppmap[Z,size_t].iterator it = mymap.begin()
        self.dims = dict()
        while it != mymap.end():
            self.dims[_Z_to_int(deref(it).first)] = deref(it).second
            incr(it)

        self.Z64_genus_is_set = False

        self.hecke = dict()

    def dimensions(self):
        return self.dims

    def seed(self):
        return self.seed_

    def isometry_sequence(self, p, precise=True):
        prime = Integer(p)

        if not prime.is_prime():
            raise Exception("p is not prime.")

        if self.N % p == 0:
            raise Exception("Cannot compute Hecke matrix at primes dividing the discriminant.")

        if not precise:
            if not self.Z64_genus_is_set:
                logging.info("Converting arbitrary precision Genus object to fixed-precision Genus object...")
                self.Z64_genus = make_shared[Genus[Z64]](deref(self.Z_genus))
                self.Z64_genus_is_set = True

            return self._isometry_sequence_imprecise(prime)
        else:
            return self._isometry_sequence_precise(prime)

    def _isometry_sequence_imprecise(self, Integer p):
        cdef shared_ptr[IsometrySequence[W16,W32,Z64]] sequence
        cdef Z64 prime = p
        sequence = make_shared[IsometrySequence[W16,W32,Z64]](self.Z64_genus, prime)

        cdef IsometrySequenceData[Z64] data
        while not deref(sequence).done():
            data = deref(sequence).next()

            a11 = Integer(data.isometry.a11)
            a12 = Integer(data.isometry.a12)
            a13 = Integer(data.isometry.a13)
            a21 = Integer(data.isometry.a21)
            a22 = Integer(data.isometry.a22)
            a23 = Integer(data.isometry.a23)
            a31 = Integer(data.isometry.a31)
            a32 = Integer(data.isometry.a32)
            a33 = Integer(data.isometry.a33)
            s = matrix(Integers(), 3, [ a11, a12, a13, a21, a22, a23, a31, a32, a33 ])

            retval = dict()
            retval['isometry'] = s
            retval['denominator'] = Integer(data.denominator)
            retval['src'] = Integer(data.src)
            retval['dst'] = Integer(data.dst)
            yield retval

    def _isometry_sequence_precise(self, Integer p):
        cdef shared_ptr[IsometrySequence[W16,W32,Z]] sequence
        sequence = make_shared[IsometrySequence[W16,W32,Z]](self.Z_genus, Z(p.value))

        cdef IsometrySequenceData[Z] data
        while not deref(sequence).done():
            data = deref(sequence).next()
            a11 = _Z_to_int(data.isometry.a11)
            a12 = _Z_to_int(data.isometry.a12)
            a13 = _Z_to_int(data.isometry.a13)
            a21 = _Z_to_int(data.isometry.a21)
            a22 = _Z_to_int(data.isometry.a22)
            a23 = _Z_to_int(data.isometry.a23)
            a31 = _Z_to_int(data.isometry.a31)
            a32 = _Z_to_int(data.isometry.a32)
            a33 = _Z_to_int(data.isometry.a33)
            s = matrix(Integers(), 3, [ a11, a12, a13, a21, a22, a23, a31, a32, a33 ])

            retval = dict()
            retval['isometry'] = s
            retval['denominator'] = _Z_to_int(data.denominator)
            retval['src'] = Integer(data.src)
            retval['dst'] = Integer(data.dst)
            yield retval

    def hecke_matrix(self, p, conductor, sparse=None, precise=True):
        prime = Integer(p)

        if not prime.is_prime():
            raise Exception("p is not prime.")

        if self.N % conductor != 0:
            raise Exception("Conductor must divide the discriminant.")

        if self.N % p == 0:
            raise Exception("Cannot compute Hecke matrix at primes dividing the discriminant.")

        if prime in self.hecke:
            if conductor in self.hecke[prime]:
                return self.hecke[prime][conductor]
            else:
                raise Exception("No Hecke matrix associated to this conductor. How did this happen?")

        if sparse is None:
            density = self._estimated_density(p, conductor)
            logging.info("Hecke matrix density estimate at p={}: {}%".format(prime, density * 100.0))
            sparse = density <= 0.3
            logging.info("Based on density estimates, computing {} matrices".format("sparse" if sparse else "dense"))

        if not precise:
            if not self.Z64_genus_is_set:
                logging.info("Converting arbitrary precision Genus object to fixed-precision Genus object...")
                self.Z64_genus = make_shared[Genus[Z64]](deref(self.Z_genus))
                self.Z64_genus_is_set = True

            start_time = datetime.now()
            if sparse:
                logging.info("Computing Hecke matrices (p={}, sparse, int64_t)...".format(prime))
                self._hecke_matrix_sparse_imprecise(prime)
            else:
                logging.info("Computing Hecke matrices (p={}, dense, int64_t)...".format(prime))
                self._hecke_matrix_dense_imprecise(prime)
        else:
            start_time = datetime.now()
            if sparse:
                logging.info("Computing Hecke matrices (p={}, sparse, arbitrary)...".format(prime))
                self._hecke_matrix_sparse_precise(prime)
            else:
                logging.info("Computing Hecke matrices (p={}, dense, arbitrary)...".format(prime))
                self._hecke_matrix_dense_precise(prime)
        end_time = datetime.now()
        logging.info("Finished computing Hecke matrices at p={} (time: {})".format(prime, end_time-start_time))

        if conductor in self.hecke[prime]:
            return self.hecke[prime][conductor]
        else:
            raise Exception("No Hecke matrix associated to this conductor. How did this happen?")

    def _hecke_matrix_dense_precise(self, Integer p):
        cdef cppmap[Z,vector[int]] mymap
        cdef cppmap[Z,vector[int]].iterator it

        try:
            start_time = datetime.now()
            mymap = deref(self.Z_genus).hecke_matrix_dense(Z(p.value))
            end_time = datetime.now()
            logging.info("  call time: {}".format(end_time-start_time))
            self.hecke[p] = dict()
        except Exception as e:
            raise Exception(e.message)

        start_time = datetime.now()
        it = mymap.begin()
        while it != mymap.end():
            cond = _Z_to_int(deref(it).first)
            self.hecke[p][cond] = _make_matrix(self.dims[cond], deref(it).second)
            incr(it)

        end_time = datetime.now()
        logging.info("  copy time: {}".format(end_time-start_time))

    def _hecke_matrix_dense_imprecise(self, Integer p):
        cdef cppmap[Z64,vector[int]] mymap
        cdef cppmap[Z64,vector[int]].iterator it

        try:
            start_time = datetime.now()
            mymap = deref(self.Z64_genus).hecke_matrix_dense(p)
            end_time = datetime.now()
            logging.info("  call time: {}".format(end_time-start_time))
            self.hecke[p] = dict()
        except Exception as e:
            raise Exception(e.message)

        start_time = datetime.now()
        it = mymap.begin()
        while it != mymap.end():
            cond = Integer(deref(it).first)
            self.hecke[p][cond] = _make_matrix(self.dims[cond], deref(it).second)
            incr(it)

        end_time = datetime.now()
        logging.info("  copy time: {}".format(end_time-start_time))

    def _hecke_matrix_sparse_precise(self, Integer p):
        cdef cppmap[Z,vector[vector[int]]] mymap
        cdef cppmap[Z,vector[vector[int]]].iterator it

        try:
            start_time = datetime.now()
            mymap = deref(self.Z_genus).hecke_matrix_sparse(Z(p.value))
            end_time = datetime.now()
            logging.info("  call time: {}".format(end_time-start_time))
            self.hecke[p] = dict()
        except Exception as e:
            raise Exception(e.message)

        cdef int data_len
        cdef int indices_len
        cdef int intptr_len

        start_time = datetime.now()
        it = mymap.begin()
        while it != mymap.end():
            cond = _Z_to_int(deref(it).first)
            dim = self.dims[cond]

            mat = csr_matrix((np.array([]), np.array([]), np.zeros(dim+1)), shape=(dim,dim))

            data_len = deref(it).second[0].size()
            indices_len = deref(it).second[1].size()
            indptr_len = deref(it).second[2].size()

            mat.data = _make_array(data_len, deref(it).second[0])
            mat.indices = _make_array(indices_len, deref(it).second[1])
            mat.indptr = _make_array(indptr_len, deref(it).second[2])

            self.hecke[p][cond] = mat

            incr(it)

        end_time = datetime.now()
        logging.info("  copy time: {}".format(end_time-start_time))

    def _hecke_matrix_sparse_imprecise(self, Integer p):
        cdef cppmap[Z64,vector[vector[int]]] mymap
        cdef cppmap[Z64,vector[vector[int]]].iterator it

        try:
            start_time = datetime.now()
            mymap = deref(self.Z64_genus).hecke_matrix_sparse(p)
            end_time = datetime.now()
            logging.info("  call time: {}".format(end_time-start_time))
            self.hecke[p] = dict()
        except Exception as e:
            raise Exception(e.message)

        cdef int data_len
        cdef int indices_len
        cdef int intptr_len

        start_time = datetime.now()
        it = mymap.begin()
        while it != mymap.end():
            cond = Integer(deref(it).first)
            dim = self.dims[cond]

            mat = csr_matrix((np.array([]), np.array([]), np.zeros(dim+1)), shape=(dim,dim))

            data_len = deref(it).second[0].size()
            indices_len = deref(it).second[1].size()
            indptr_len = deref(it).second[2].size()

            mat.data = _make_array(data_len, deref(it).second[0])
            mat.indices = _make_array(indices_len, deref(it).second[1])
            mat.indptr = _make_array(indptr_len, deref(it).second[2])

            self.hecke[p][cond] = mat

            incr(it)

        end_time = datetime.now()
        logging.info("  copy time: {}".format(end_time-start_time))

    def _estimated_density(self, p, conductor):
        if p == 2: p+1
        M = (p+1) / 2
        N = self.dims[conductor]
        if conductor == 1:
            return 1.0 - exp(2 * M * (log(N-1) - log(N)))

        lg2M1 = lgamma(2 * M + 1)
        log4 = log(4)

        logN1 = log(N-1)
        logN = log(N)

        density = 0.0
        for i in range(M):
            logprob = 2*(M-i)*logN1 + lg2M1 - (2*M*logN +
                2*lgamma(i+1) + i*log4 + lgamma(2*M-2*i+1))
            e = exp(logprob)
            if e == 0: break
            density += e

        return 1.0 - density

    def __repr__(self):
        return '''Birch genus with discriminant {} = {}
Ramified primes = {}
Dimensions = {}
Seed = {}'''.format(self.N, self.facs, self.ramified_primes, self.dims, self.seed_)

cdef _Z_to_int(const Z& x):
    return Integer(x.get_str(), 10)


cdef class _MatrixWrapper:
    cdef vector[int] vec
    cdef Py_ssize_t shape[2]
    cdef Py_ssize_t strides[2]
    cpdef dim

    def __init__(self, dim=0):
        self.dim = dim

    cdef set_data(self, vector[int]& data):
        self.vec = move(data)

    def __getbuffer__(self, Py_buffer *buffer, int flags):
        cdef Py_ssize_t itemsize = sizeof(self.vec[0])

        self.shape[0] = self.dim
        self.shape[1] = self.dim
        self.strides[0] = sizeof(int) * self.dim
        self.strides[1] = sizeof(int)
        buffer.buf = <char *>&(self.vec[0])
        buffer.format = 'i'
        buffer.internal = NULL
        buffer.itemsize = itemsize
        buffer.len = self.dim * self.dim * itemsize
        buffer.ndim = 2
        buffer.obj = self
        buffer.readonly = 0
        buffer.shape = self.shape
        buffer.strides = self.strides
        buffer.suboffsets = NULL

cdef class _ArrayWrapper:
    cdef vector[int] vec
    cdef Py_ssize_t shape[1]
    cdef Py_ssize_t strides[1]
    cpdef dim

    def __init__(self, dim=0):
        self.dim = dim

    cdef set_data(self, vector[int]& data):
        self.vec = move(data)

    def __getbuffer__(self, Py_buffer *buffer, int flags):
        cdef Py_ssize_t itemsize = sizeof(self.vec[0])

        self.shape[0] = self.dim
        self.strides[0] = sizeof(int)
        buffer.buf = <char *>&(self.vec[0])
        buffer.format = 'i'
        buffer.internal = NULL
        buffer.itemsize = itemsize
        buffer.len = self.dim * itemsize
        buffer.ndim = 1
        buffer.obj = self
        buffer.readonly = 0
        buffer.shape = self.shape
        buffer.strides = self.strides
        buffer.suboffsets = NULL

cdef _make_array(dim, vector[int]& data):
    cdef _ArrayWrapper aw
    aw = _ArrayWrapper(dim)
    aw.set_data(data)
    return np.asarray(aw)

cdef _make_matrix(dim, vector[int]& data):
    cdef _MatrixWrapper mw
    mw = _MatrixWrapper(dim)
    mw.set_data(data)
    return np.asarray(mw)

cdef do_something(const IsometrySequenceData[Z]& data):
    print data.src, data.dst
