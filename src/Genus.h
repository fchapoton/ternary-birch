#ifndef __GENUS_H_
#define __GENUS_H_

#include "birch.h"
#include "Math.h"
#include "HashMap.h"
#include "Spinor.h"
#include "Isometry.h"
#include "NeighborManager.h"

template<typename R>
class GenusRep
{
public:
    GenusRep() = default;
    GenusRep(const GenusRep<R>& genus) = default;
    GenusRep(GenusRep<R>&& genus) = default;

    QuadForm<R> q;
    Isometry<R> s;
    Isometry<R> sinv;
    Z64 parent;
    R p;
    std::map<R,int> es;
};

template<typename R>
class Genus
{
    template<typename T>
    friend class Genus;

public:
    Genus() = default;

    Genus(const QuadForm<R>& q, const std::vector<PrimeSymbol<R>>& symbols, W64 seed = 0)
    {
        if (seed == 0)
        {
            std::random_device rd;
            seed = rd();
        }

        this->disc = q.discriminant();
        this->seed_ = seed;

        this->prime_divisors.reserve(symbols.size());
        for (const PrimeSymbol<R>& symb : symbols)
        {
            this->prime_divisors.push_back(symb.p);
        }

        Spinor<R> *spin = new Spinor<R>(this->prime_divisors);
        this->spinor = std::unique_ptr<Spinor<R>>(spin);

        if (symbols.size() > 63)
        {
            throw std::invalid_argument("Must have 63 or fewer prime divisors.");
        }

        size_t num_conductors = 1LL << symbols.size();

        this->conductors.reserve(num_conductors);
        this->conductors.push_back(1);

        size_t bits = 0;
        size_t mask = 1;
        for (size_t n=1; n<num_conductors; n++)
        {
            if (n == 2*mask)
            {
                ++bits;
                mask = 1LL << bits;
            }
            R value = this->prime_divisors[bits] * this->conductors[n ^ mask];
            this->conductors.push_back(value);
        }

        GenusRep<R> rep;
        rep.q = q;
        rep.p = 1;
        rep.parent = -1;

        // Set the mass as a multiple of 24, as this is the largest integer
        // that can appear in its denominator. This value is used to determine
        // when the genus has been fully populated.
        this->mass_x24 = this->get_mass(q, symbols);

        // The mass provides a reasonable estimate for the size of the genus
        // since most isometry classes typically have trivial automorphism
        // group.
        Z64 estimated_size = ceil(mpz_get_d(this->mass_x24.get_mpz_t()) / 24.0);
        auto *ptr = new HashMap<GenusRep<R>>(estimated_size);
        this->hash = std::unique_ptr<HashMap<GenusRep<R>>>(ptr);
        this->hash->add(rep);

        // The spinor primes hash table, used to identify the primes used in
        // constructing the genus representatives.
        auto *ptr2 = new HashMap<W16>();
        this->spinor_primes = std::unique_ptr<HashMap<W16>>(ptr2);

        Z sum_mass_x24 = (48 / QuadForm<R>::num_automorphisms(q));

        Z p = 1;
        W16 prime = 1;

        // A temporary placeholder for the genus representatives before they
        // are fully built.
        GenusRep<R> foo;

        bool done = (sum_mass_x24 == this->mass_x24);
        while (!done)
        {
            // Get the next good prime and build the appropriate finite field.
            do
            {
                mpz_nextprime(p.get_mpz_t(), p.get_mpz_t());
                prime = mpz_get_ui(p.get_mpz_t());
            }
            while (this->disc % prime == 0);
            std::shared_ptr<W16_Fp> GF;
            if (prime == 2)
                GF = std::make_shared<W16_F2>(prime, this->seed_);
            else
                GF = std::make_shared<W16_Fp>(prime, this->seed_, true);

            size_t current = 0;
            while (!done && current < this->hash->size())
            {
                // Get the current quadratic form and build the neighbor manager.
                const QuadForm<R>& mother = this->hash->get(current).q;
                NeighborManager<W16,W32,R> manager(mother, GF);

                #ifdef DEBUG
                // Build the affine quadratic form for debugging purposes.
                W16_QuadForm qp = mother.mod(GF);
                #endif

                for (W16 t=0; !done && t<=prime; t++)
                {
                    #ifdef DEBUG
                    // Verify that the appropriate vector is isotropic.
                    W16_Vector3 vec = manager.isotropic_vector(t);
                    assert( qp.evaluate(vec) % prime == 0 );
                    #endif

                    // Construct the neighbor, the isometry is stored in s.
                    foo.s.set_identity();
                    foo.q = manager.get_neighbor(t, foo.s);

                    #ifdef DEBUG
                    // Verify neighbor discriminant matches.
                    assert( rep.q.discriminant() == mother.discriminant() );
                    #endif

                    // Reduce the neighbor to its Eisenstein form and add it to
                    // the hash table.
                    foo.q = QuadForm<R>::reduce(foo.q, foo.s);
                    foo.p = prime;
                    foo.parent = current;

                    bool added = this->hash->add(foo);
                    if (added)
                    {
                        const GenusRep<R>& temp = this->hash->last();
                        sum_mass_x24 += 48 / QuadForm<R>::num_automorphisms(temp.q);
                        done = (sum_mass_x24 == this->mass_x24);
                        this->spinor_primes->add(prime);
                    }
                }

                ++current;
            }
        }

        // Initialize the dimensions to zero, we will compute these values below.
        this->dims.resize(num_conductors, 0);

        // Create the lookup table values for each genus rep at each conductor.
        size_t genus_size = this->hash->size();
        this->lut_positions.resize(num_conductors, std::vector<int>(genus_size, -1));

        // The genus rep isometries were initialized only to contain the
        // isometry between the parent and its child, we now want to update
        // these isometries so that they are rational isometries between the
        // "mother" quadratic form and the genus rep.
        for (size_t n=0; n<this->hash->size(); n++)
        {
            GenusRep<R>& rep = this->hash->at(n);

            // Only compute composite isometries if we are not considering the
            // mother form.
            if (n)
            {
                GenusRep<R>& parent = this->hash->at(rep.parent);

                // Construct the isometries to/from the mother quadratic form.
                rep.sinv = rep.s.inverse(rep.p);
                rep.sinv = rep.sinv * parent.sinv;
                rep.s = parent.s * rep.s;

                // Copy the numerators, and increment the genus rep prime.
                rep.es = parent.es;
                ++rep.es[rep.p];

                #ifdef DEBUG
                R scalar = birch_util::my_pow(rep.es);
                scalar *= scalar;

                // Verify that s is an isometry from the mother form to the rep,
                // and that sinv is an isometry from the rep to the mother form.
                assert( rep.s.is_isometry(q, rep.q, scalar) );
                assert( rep.sinv.is_isometry(rep.q, q, scalar) );
                #endif
            }

            // Determine which subspaces this representative contributes.
            const std::vector<Isometry<R>>& auts = QuadForm<R>::proper_automorphisms(rep.q);
            std::vector<bool> ignore(this->conductors.size(), false);
            for (const Isometry<R>& s : auts)
            {
                Z64 vals = this->spinor->norm(rep.q, s, 1);

                for (size_t k=0; k<num_conductors; k++)
                {
                    if (!ignore[k] && (birch_util::popcnt(vals & k) & 1))
                    {
                        ignore[k] = true;
                    }
                }
            }

            for (size_t k=0; k<num_conductors; k++)
            {
                if (!ignore[k])
                {
                    this->lut_positions[k][n] = this->dims[k];
                }
                this->dims[k] += (ignore[k] ? 0 : 1);
            }
        }
    }

    template<typename T>
    static Genus<T> convert(const Genus<R>& src)
    {
        //std::shared_ptr<Genus<T>> genus = std::make_shared<Genus<T>>();
        Genus<T> genus;

        // Convert the discriminant.
        genus.disc = birch_util::convert_Integer<R,T>(src.disc);

        // Convert the prime divisors.
        for (const R& p : src.prime_divisors)
        {
            genus.prime_divisors.push_back(birch_util::convert_Integer<R,T>(p));
        }

        // Convert the conductors.
        for (const R& cond : src.conductors)
        {
            genus.conductors.push_back(birch_util::convert_Integer<R,T>(cond));
        }

        // Copy dimensions.
        genus.dims = src.dims;

        // Copy lookup table dimensions.
        genus.lut_positions = src.lut_positions;

        // Copy mass.
        genus.mass_x24 = src.mass_x24;

        // Build a copy of the spinor primes hash table.
        genus.spinor_primes = std::unique_ptr<HashMap<W16>>(new HashMap<W16>(src.spinor_primes->size()));
        for (W16 x : src.spinor_primes->keys())
        {
            genus.spinor_primes->add(x);
        }

        // Build a copy of the genus representatives hash table.
        genus.hash = std::unique_ptr<HashMap<GenusRep<T>>>(new HashMap<GenusRep<T>>(src.hash->size()));
        for (const GenusRep<R>& rep : src.hash->keys())
        {
            genus.hash->add(birch_util::convert_GenusRep<R,T>(rep));
        }

        // Create Spinor class.
        std::vector<T> primes;
        primes.reserve(src.spinor->primes().size());
        for (const R& p : src.spinor->primes())
        {
            primes.push_back(birch_util::convert_Integer<R,T>(p));
        }
        genus.spinor = std::unique_ptr<Spinor<T>>(new Spinor<T>(primes));

        // Copy seed.
        genus.seed_ = src.seed_;

        return genus;
    }

    size_t size(void) const
    {
        return this->hash->keys().size();
    }

    W64 seed(void) const
    {
        return this->seed_;
    }

    std::map<R,size_t> dimension_map(void) const
    {
        std::map<R,size_t> temp;
        size_t num_conductors = this->conductors.size();
        for (size_t k=0; k<num_conductors; k++)
        {
            temp[this->conductors[k]] = this->dims[k];
        }
        return temp;
    }

    std::map<R,std::vector<int>> hecke_matrix_dense(const R& p) const
    {
        if (this->disc % p == 0)
        {
            throw std::invalid_argument("Prime must not divide the discriminant.");
        }
        return this->hecke_matrix_dense_internal(p);
    }

    std::map<R,std::vector<std::vector<int>>> hecke_matrix_sparse(const R& p) const
    {
        return this->hecke_matrix_sparse_internal(p);
    }

private:
    R disc;
    std::vector<R> prime_divisors;
    std::vector<R> conductors;
    std::vector<size_t> dims;
    std::vector<std::vector<int>> lut_positions;
    Z mass_x24;
    std::unique_ptr<HashMap<W16>> spinor_primes;
    std::unique_ptr<HashMap<GenusRep<R>>> hash;
    std::unique_ptr<Spinor<R>> spinor;
    W64 seed_;

    // TODO: Add the actual mass formula here for reference.
    Z get_mass(const QuadForm<R>& q, const std::vector<PrimeSymbol<R>>& symbols)
    {
        Z mass = 2 * this->disc;
        Z a = q.h() * q.h() - 4 * q.a() * q.b();
        Z b = -q.a() * this->disc;

        for (const PrimeSymbol<R>& symb : symbols)
        {
            mass *= (symb.p + Math<Z>::hilbert_symbol(a, b, symb.p));
            mass /= 2;
            mass /= symb.p;
        }

        return mass;
    }

    std::map<R,std::vector<std::vector<int>>> hecke_matrix_sparse_internal(const R& p) const
    {
        size_t num_conductors = this->conductors.size();
        size_t num_primes = this->prime_divisors.size();

        std::vector<std::vector<int>> data(num_conductors);
        std::vector<std::vector<int>> indptr;
        std::vector<std::vector<int>> indices(num_conductors);

        W16 prime = birch_util::convert_Integer<R,W16>(p);

        std::shared_ptr<W16_Fp> GF;
        if (prime == 2)
            GF = std::make_shared<W16_F2>(2, this->seed());
        else
            GF = std::make_shared<W16_Fp>((W16)prime, this->seed(), true);

        std::vector<W64> all_spin_vals(prime+1);

        std::vector<std::vector<int>> rowdata;
        for (int dim : this->dims)
        {
            rowdata.push_back(std::vector<int>(dim));
            indptr.push_back(std::vector<int>(dim+1, 0));
        }

        const GenusRep<R>& mother = this->hash->keys()[0];
        size_t num_reps = this->size();
        for (size_t n=0; n<num_reps; n++)
        {
            const GenusRep<R>& cur = this->hash->get(n);
            NeighborManager<W16,W32,R> manager(cur.q, GF);

            for (W16 t=0; t<=prime; t++)
            {
                GenusRep<R> foo = manager.get_reduced_neighbor_rep(t);

                #ifdef DEBUG
                assert( foo.s.is_isometry(cur.q, foo.q, p*p) );
                #endif

                size_t r = this->hash->indexof(foo);

                #ifdef DEBUG
                assert( r < this->size() );
                #endif

                W64 spin_vals;
                if (r == n)
                {
                    spin_vals = this->spinor->norm(foo.q, foo.s, p);
                }
                else
                {
                    const GenusRep<R>& rep = this->hash->get(r);
                    foo.s = cur.s * foo.s;
                    R scalar = p;

                    #ifdef DEBUG
                    R temp_scalar = p*p;
                    R temp = birch_util::my_pow(cur.es);
                    temp_scalar *= temp * temp;
                    assert( foo.s.is_isometry(mother.q, foo.q, temp_scalar) );
                    #endif

                    foo.s = foo.s * rep.sinv;

                    #ifdef DEBUG
                    temp = birch_util::my_pow(rep.es);
                    temp_scalar *= temp * temp;
                    assert( foo.s.is_isometry(mother.q, mother.q, temp_scalar) );
                    #endif

                    scalar *= birch_util::my_pow(cur.es);
                    scalar *= birch_util::my_pow(rep.es);

                    #ifdef DEBUG
                    assert( scalar*scalar == temp_scalar );
                    #endif

                    spin_vals = this->spinor->norm(mother.q, foo.s, scalar);
                }

                all_spin_vals[t] = (r << num_primes) | spin_vals;
            }

            for (size_t k=0; k<num_conductors; k++)
            {
                const std::vector<int>& lut = this->lut_positions[k];
                int npos = lut[n];
                if (npos == -1) continue;

                // Populate the row data.
                std::vector<int>& row = rowdata[k];
                for (W64 x : all_spin_vals)
                {
                    int r = x >> num_primes;
                    int rpos = lut[r];
                    if (rpos == -1) continue;

                    int value = birch_util::char_val(x & k);
                    row[rpos] += value;
                }

                // Update data and indices with the nonzero values.
                size_t nnz = 0;
                size_t pos = 0;
                std::vector<int>& data_k = data[k];
                std::vector<int>& indices_k = indices[k];
                for (int x : row)
                {
                    if (x)
                    {
                        data_k.push_back(x);
                        indices_k.push_back(pos);
                        row[pos] = 0; // Clear the nonzero entry.
                        ++nnz;
                    }
                    ++pos;
                }

                // Update indptr
                indptr[k][npos+1] = indptr[k][npos] + nnz;
            }
        }

        std::map<R,std::vector<std::vector<int>>> csr_matrices;
        for (size_t k=0; k<num_conductors; k++)
        {
            const R& cond = this->conductors[k];
            csr_matrices[cond] = std::vector<std::vector<int>>();
            csr_matrices[cond].push_back(data[k]);
            csr_matrices[cond].push_back(indices[k]);
            csr_matrices[cond].push_back(indptr[k]);
        }
        return csr_matrices;
    }

    std::map<R,std::vector<int>> hecke_matrix_dense_internal(const R& p) const
    {
        size_t num_conductors = this->conductors.size();
        size_t num_primes = this->prime_divisors.size();

        // Allocate memory for the Hecke matrices and create a vector to store
        // pointers to the raw matrix data.
        std::vector<int*> hecke_ptr;
        hecke_ptr.reserve(num_conductors);
        std::vector<std::vector<int>> hecke_matrices;
        for (size_t k=0; k<num_conductors; k++)
        {
            size_t dim = this->dims[k];
            hecke_matrices.push_back(std::vector<int>(dim * dim));
            hecke_ptr.push_back(hecke_matrices.back().data());
        }

        W16 prime = birch_util::convert_Integer<R,W16>(p);
        std::vector<W64> all_spin_vals(prime+1);

        std::shared_ptr<W16_Fp> GF;
        if (prime == 2)
            GF = std::make_shared<W16_F2>(2, this->seed());
        else
            GF = std::make_shared<W16_Fp>((W16)prime, this->seed(), true);

        const GenusRep<R>& mother = this->hash->keys()[0];
        size_t num_reps = this->size();
        for (size_t n=0; n<num_reps; n++)
        {
            const GenusRep<R>& cur = this->hash->get(n);
            NeighborManager<W16,W32,R> manager(cur.q, GF);

            for (W16 t=0; t<=prime; t++)
            {
                GenusRep<R> foo = manager.get_reduced_neighbor_rep(t);

                #ifdef DEBUG
                assert( foo.s.is_isometry(cur.q, foo.q, p*p) );
                #endif

                size_t r = this->hash->indexof(foo);

                #ifdef DEBUG
                assert( r < this->size() );
                #endif

                W64 spin_vals;
                if (unlikely(r == n))
                {
                    spin_vals = this->spinor->norm(foo.q, foo.s, p);
                }
                else
                {
                    const GenusRep<R>& rep = this->hash->get(r);
                    foo.s = cur.s * foo.s;
                    R scalar = p;

                    #ifdef DEBUG
                    R temp_scalar = p*p;
                    R temp = birch_util::my_pow(cur.es);
                    temp_scalar *= temp * temp;
                    assert( foo.s.is_isometry(mother.q, foo.q, temp_scalar) );
                    #endif

                    foo.s = foo.s * rep.sinv;

                    #ifdef DEBUG
                    temp = birch_util::my_pow(rep.es);
                    temp_scalar *= temp * temp;
                    assert( foo.s.is_isometry(mother.q, mother.q, temp_scalar) );
                    #endif

                    scalar *= birch_util::my_pow(cur.es);
                    scalar *= birch_util::my_pow(rep.es);

                    #ifdef DEBUG
                    assert( scalar*scalar == temp_scalar );
                    #endif

                    spin_vals = this->spinor->norm(mother.q, foo.s, scalar);
                }

                all_spin_vals[t] = (r << num_primes) | spin_vals;
            }

            for (size_t k=0; k<num_conductors; k++)
            {
                const std::vector<int>& lut = this->lut_positions[k];
                int npos = lut[n];
                if (unlikely(npos == -1)) continue;

                int *row = hecke_ptr[k];

                for (W64 x : all_spin_vals)
                {
                    int r = x >> num_primes;
                    int rpos = lut[r];
                    if (unlikely(rpos == -1)) continue;

                    row[rpos] += birch_util::char_val(x & k);
                }

                hecke_ptr[k] += this->dims[k];
            }
        }

        // Move all of the Hecke matrices into an associative map and return.
        std::map<R,std::vector<int>> matrices;
        for (size_t k=0; k<num_conductors; k++)
        {
            matrices[this->conductors[k]] = std::move(hecke_matrices[k]);
        }
        return matrices;
    }
};

template<typename R>
bool operator==(const GenusRep<R>& a, const GenusRep<R>& b)
{
    return a.q == b.q;
}

#endif // __GENUS_H_
