Ternary Birch
=============
A library for computing Hecke matrices for positive definite rational ternary quadratic forms.

The code in this repository is based on my Ph.D thesis and is an ongoing project.

- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Contributing](#contribution)

## Requirements

- [autotools](https://www.gnu.org/software/automake/manual/html_node/Autotools-Introduction.html)
- [gcc](https://gcc.gnu.org/)
- [gmp](https://gmplib.org/)
- [sage](http://www.sagemath.org/)

## Installation

This can be installed either as a C++ library (libbirch.so) or compiled directly for use within a Sage session.

### As a C++ library

To build as a C++ library on a standard Linux system with autotools:

    ./autogen.sh
    ./configure --prefix=DIR
    make
    make install
    
### As a Sage module

See [Usage](#usage).
    
## Usage

While this library can compiled into a C++ library and used within applications, it was specifically designed to be integrated into Sage.

### Building for use within Sage

To use in Sage, you will need to install this package to your local Sage distribution. Within the ``src'' directory, run the following command:

    sage setup.py install --user

Once this finishes, from within Sage:

    sage: import ternary_birch

### Constructing a genus:

A genus for the desired level can be generated by instantiating a ``BirchGenus`` object.

    sage: g = BirchGenus(11*13*17*19*23)

By default, this will construct a ternary quadratic form with the specified discriminant. The quadratic form is constructed so that as many of its primes are ramified in its associated quaternion order. If there are an odd number of prime factors, all primes will be ramified; if there are an even number of prime factors, all but the largest prime will ramify.

You can override the default ramification behavior by specifying the ``ramified_primes`` keyword argument:

    sage: h = BirchGenus(11*13*17*19*23, ramified_primes=[11,13,17])
    
### Computing Hecke matrices

With a genus object constructed, Hecke matrices at primes not dividing the discriminant can be computed for each conductor, a squarefree divisor of the discriminant, using the ``hecke_matrix`` member function. These are Hecke matrices with weight associated to a twist by a Kronecker character whose conductor is a squarefree divisor of the discriminant.

    sage: A = g.hecke_matrix(101, 17*19)

This returns the Hecke matrix for conductor ``323 = 17*19`` at the prime ``101``. Despite only returning a single matrix, Hecke matrices for each squarefree conductor have also been computed and stored in memory. These matrices are accessible by varying the conductor parameter.

    sage: B = g.hecke_matrix(101, 11*17)

By default, the matrices returned by ``hecke_matrix`` are either sparse (``scipy.csr_matrix`` objects) or dense (``numpy.ndarray`` objects). This behavior is determined by an internal density estimate, and can be overridden with the ``sparse`` keyword argument.

    sage: C = g.hecke_matrix(71, 11*17, sparse=False)

Additionally, the underlying arithmetic within ``hecke_matrix`` is done using multi-precision arithmetic. For sufficiently small discriminants and primes, it may be advantageous to use native 64-bit arithmetic in computing Hecke matrices. This behavior can be overriden with the ``precise`` keyword argument.

    sage: D = g.hecke_matrix(73, 11*17, precise=False)

There is a mechanism in place to catch overflow issues when they occur, namely when computing p-neighbors, if the discriminants do not match an ``OverflowError`` exception is thrown.

### Computing Hecke eigenvalues

With a genus object constructed, you can recover its rational eigenvectors as follows:

    sage: g = BirchGenus(5 * 7 * 11 * 13 * 17)
    sage: evecs = g.rational_eigenvectors()

This will return a list of dictionaries corresponding to each rational eigenvector. Each dictionary contains three keywords: ``aps``, ``vector``, and ``conductor``.
- The ``aps`` keyword contains the vector's known eigenvalues, this is a dictionary indexed by the corresponding prime.
- The ``vector`` keyword contains the column vector for the associated Hecke matrices.
- The ``conductor`` keyword contains an integer corresponding to the conductor of the character from which it arose.

The rational eigenvectors are stored internally so that additional calls to ``rational_eigenvectors`` do nothing. You can force recomputation of the rational eigenvectors with the ``force=True`` parameter.

As with computing Hecke matrices, you can pass the ``precise`` keyword to compute eigenvectors for reasonably small levels. **Note:** This only affects computing Hecke matrices and not the underlying linear algebra.

    sage: evecs = g.rational_eigenvectors(precise=False)

Once rational eigenvectors have been computed, you are ready to compute Hecke eigenvalues.

    sage: g.compute_eigenvalues(101)

This function has no return value, but instead updates the internal state of the eigenvectors stored within the genus object. By printing the contents of ``evecs`` again, you can see that the eigenvalues are now present.

    sage: print(evecs)

You may also compute all eigenvalues up to a specific bound.

    sage: g.compute_eigenvalues_upto(1000)

Additionally, the ``compute_eigenvalues`` and ``compute_eigenvalues_upto`` functions accept a ``precise=False`` keyword argument to allow for even faster eigenvalue computations for reasonaly small level and primes.

### Accessing isometries for building custom Hecke matrices

With a genus object constructed, isometries used within the ``hecke_matrix`` functions can be directly accessed via the ``isometry_sequence`` member function. This data can then be used to manually compute custom Hecke operators with a user-defined representation. For example, in Sage:

    def trivial_representation(isometry):
         return 1

    import numpy as np
    g = BirchGenus(11)
    dim = g.dimensions()[1]
    mat = np.zeros((dim, dim), dtype=np.int32)
    for entry in g.isometry_sequence(31):
        row = entry['src']
        col = entry['dst']
        mat[row][col] += trivial_representation(entry)

Compare this to:

    sage: mat == g.hecke_matrix(31, 1)

This feature is still under development.

### Resuming sessions using seeds

By default, when ``BirchGenus`` is constructed, a random seed is established to aid in some of the probabalistic algorithms within. This can cause the ordering of the genus representatives to be permuted between separate sessions using the same input level. Due to this, a ``seed`` parameter can be saved, allowing for the same genus ordering in a later session.

    sage: my_seed = g.seed()

In a later session, this can then be provided upon constructing your ``BirchGenus`` object.

    sage: h = BirchGenus(11*13*17*19*23, seed=my_seed)

## Contributing

If you want to help develop this project, please create your own fork on Github and submit a pull request. I will do my best to integrate any additional useful features as necessary. Alternatively, submit a patch to me via email at jefferyphein@gmail.com.
