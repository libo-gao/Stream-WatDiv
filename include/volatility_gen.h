#ifndef VOLATILITY_GEN_H
#define VOLATILITY_GEN_H

#include <random>
#include <vector>

using namespace std;

namespace VOLATILITY_MODEL {
    enum enum_t {
        NORMAL_DIST,
        LAPLACE_DIST,
        LOGISTIC_DIST,
        CAUCHY_DIST,
        UNDEFINED
    };
};


class volatility_gen{
    public:
        volatility_gen(const char * frequency_sample_file, double location, VOLATILITY_MODEL::enum_t distribution, const char * volatility_sample_file);
        volatility_gen(const volatility_gen & rhs);
        ~volatility_gen();

        void initialize (int model_size);
        bool is_initialized () const;

        int get_model_size () const;

        double get_initial_frequency_value (int index) const;
        double get_volatility_value (int index) const;
        double get_next_ln_increase (int index) const;
        double get_probability (int index) const;

        int next_rand_index () const;

        double advance ();
        void warmup ();

        static volatility_gen * parse (const string & line, string & name, float & advance_pr);

        static void test ();
    private:
        mt19937 * _rd_gen;
        uniform_real_distribution<> * _uniform_real_distribution;

        bool _instantiated;
        int _model_size;
        double _location;
        double _max_frequency;
        VOLATILITY_MODEL::enum_t _distribution;
        vector<double> _frequency_sample;
        vector<double> _gen_frequency;
        vector<double> _volatility_sample;
        vector<double> _gen_volatility;

        double * _dynamic_frequency_array;
        double * _cum_density_array;

        double next_uniform() const;

        template <typename T> int sgn(T val) const {
            return (T(0) < val) - (val < T(0));
        }
};

#endif // VOLATILITY_GEN_H
