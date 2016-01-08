#include "volatility_gen.h"

#include <boost/lexical_cast.hpp>
#include <boost/math/constants/constants.hpp>
#include <boost/math/special_functions/erf.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

using namespace std;

volatility_gen::volatility_gen(const char * frequency_sample_file, double location, VOLATILITY_MODEL::enum_t distribution, const char * volatility_sample_file){
    random_device rd;
    _rd_gen = new mt19937 (rd());
    _uniform_real_distribution = new uniform_real_distribution<> (0, 1);

    _instantiated = false;
    _model_size = 0;

    _location = location;
    _distribution = distribution;

    string token;
    ifstream ifs_freq (frequency_sample_file);
    if (!ifs_freq){
        cerr << "[volatility_gen::volatility_gen]\tFile "<<frequency_sample_file<<" does not exist..." << "\n";
        exit(0);
    }
    while (ifs_freq>>token){
        double freq_value = boost::lexical_cast<double>(token);
        _frequency_sample.push_back(freq_value);
    }
    ifs_freq.close();
    sort(_frequency_sample.begin(), _frequency_sample.end());

    bool zero_included = false;
    ifstream ifs_vol (volatility_sample_file);
    if (!ifs_vol){
        cerr << "[volatility_gen::volatility_gen]\tFile "<<volatility_sample_file<<" does not exist..." << "\n";
        exit(0);
    }
    while (ifs_vol>>token){
        double vol_value = boost::lexical_cast<double>(token);
        if (vol_value==0.0){
            zero_included = true;
        }
        _volatility_sample.push_back(vol_value);
    }
    ifs_vol.close();
    if (!zero_included){
        _volatility_sample.push_back(0.0);
    }
    sort(_volatility_sample.begin(), _volatility_sample.end());
}

volatility_gen::volatility_gen(const volatility_gen & rhs){
    random_device rd;
    _rd_gen = new mt19937 (rd());
    _uniform_real_distribution = new uniform_real_distribution<> (0, 1);

    _instantiated = rhs._instantiated;
    _model_size = rhs._model_size;
    _location = rhs._location;
    _max_frequency = rhs._max_frequency;
    _distribution = rhs._distribution;
    _frequency_sample.insert(_frequency_sample.begin(), rhs._frequency_sample.cbegin(), rhs._frequency_sample.cend());
    _gen_frequency.insert(_gen_frequency.begin(), rhs._gen_frequency.cbegin(), rhs._gen_frequency.cend());
    _volatility_sample.insert(_volatility_sample.begin(), rhs._volatility_sample.cbegin(), rhs._volatility_sample.cend());
    _gen_volatility.insert(_gen_volatility.begin(), rhs._gen_volatility.cbegin(), rhs._gen_volatility.cend());

    _dynamic_frequency_array = new double [_model_size];
    _cum_density_array = new double [_model_size];
    for (int i=0; i<_model_size; i++){
        _dynamic_frequency_array[i] = rhs._dynamic_frequency_array[i];
        _cum_density_array[i] = rhs._cum_density_array[i];
    }
}

volatility_gen::~volatility_gen(){
    delete _rd_gen;
    delete _uniform_real_distribution;
    if (_model_size>0){
        delete [] _cum_density_array;
        delete [] _dynamic_frequency_array;
    }
}

void volatility_gen::initialize (int model_size){
    _model_size = model_size;
    _dynamic_frequency_array = new double [_model_size];
    _cum_density_array = new double[_model_size];
    _max_frequency = 0.0;

    double total_frequency = 0.0;
    for (int i=0; i<_model_size; i++){
        double rand_index = next_uniform() * (double)(_frequency_sample.size()-1);
        double range_min = _frequency_sample[floor(rand_index)];
        double range_max = _frequency_sample[ceil(rand_index)];
        double interpolated = range_min + ((range_max - range_min) * (rand_index - floor(rand_index)));
        if (interpolated > _max_frequency){
            _max_frequency = interpolated;
        }
        _gen_frequency.push_back(interpolated);
        _dynamic_frequency_array[i] = interpolated;
        total_frequency = total_frequency + interpolated;
    }
    for (int i=0; i<_model_size; i++){
        double rand_index = next_uniform() * (double)(_volatility_sample.size()-1);
        double range_min = _volatility_sample[floor(rand_index)];
        double range_max = _volatility_sample[ceil(rand_index)];
        double interpolated = range_min + ((range_max - range_min) * (rand_index - floor(rand_index)));
        _gen_volatility.push_back(interpolated);
    }

    for (int i=0; i<_model_size; i++){
        if (i==0){
            _cum_density_array[i] = _dynamic_frequency_array[i] / total_frequency;
        } else {
            _cum_density_array[i] = _cum_density_array[i-1] + (_dynamic_frequency_array[i] / total_frequency);
        }
    }

    _max_frequency = _max_frequency * 1000;
    _instantiated = true;

    /// Some debugging...
    //for (int i=0; i<_model_size; i++){
        //cout << "Volatility " << i << "=" << _gen_volatility[i] << "\n";
    //}

    warmup();
}

bool volatility_gen::is_initialized () const{
    return _instantiated;
}

double volatility_gen::advance (){
    double total_frequency = 0.0;
    for (int i=0; i<_model_size; i++){
        double ln_inc = get_next_ln_increase(i);
        _dynamic_frequency_array[i] = _dynamic_frequency_array[i] * exp(ln_inc);
        if (_dynamic_frequency_array[i] < 1.0){
            _dynamic_frequency_array[i] = 1.0;
        }
        if (_dynamic_frequency_array[i] > _max_frequency){
            _dynamic_frequency_array[i] = _max_frequency;
        }
        total_frequency += _dynamic_frequency_array[i];

        //cout << "log-increase [" << i << "] = " << ln_inc << "\n";
        //cout << "ovr-increase [" << i << "] = " << exp(ln_inc) << "\n";
        //cout << "frequency [" << i << "] = " << _dynamic_frequency_array[i] << "\n";
    }
    for (int i=0; i<_model_size; i++){
        if (i==0){
            _cum_density_array[i] = _dynamic_frequency_array[i] / total_frequency;
        } else {
            _cum_density_array[i] = _cum_density_array[i-1] + (_dynamic_frequency_array[i] / total_frequency);
        }

        //cout << "probability [" << i << "] = " << (_dynamic_frequency_array[i] / total_frequency) << "\n";
    }
    //cout << "Total frequency = " << total_frequency << "\n";
    cerr << "Advancing..." << "\n";
    return total_frequency;
}

void volatility_gen::warmup (){
    cerr << "[volatility_gen::warmup()]\tWarmup phase has started..." << "\n";

    int iteration_limit = 1000;
    int window_size = 5;
    double termination_threshold = 0.01;

    vector<double> history;
    double prev_total = 1;
    for (int i=0; i<iteration_limit; i++){
        double cur_total = advance();
        double log_increase = log(cur_total / prev_total);
        prev_total = cur_total;
        history.push_back(log_increase);
        if (history.size()>=window_size){
            double sum = 0.0;
            for (int j=(history.size()-window_size); j<history.size(); j++){
                sum += history[j];
            }
            double average = sum / ((double) window_size);
            if (average < termination_threshold){
                cerr << "[volatility_gen::warmup()]\tWarmup phase ended after " << i << " iterations..." << "\n";
                break;
            }
        }
    }
}

int volatility_gen::get_model_size () const{
    return _gen_volatility.size();
}

double volatility_gen::get_initial_frequency_value (int index) const{
    return _gen_frequency[index];
}

double volatility_gen::get_volatility_value (int index) const{
    return _gen_volatility[index];
}

double volatility_gen::get_next_ln_increase (int index) const{
    double pr = next_uniform();
    double result = 0.0;
    switch (_distribution){
        case VOLATILITY_MODEL::NORMAL_DIST:{
            result = _location + (_gen_volatility[index] * sqrt(2.0) * boost::math::erf_inv((2 * pr) - 1.0));
            break;
        }
        case VOLATILITY_MODEL::CAUCHY_DIST:{
            result = _location + (_gen_volatility[index] * tan(boost::math::constants::pi<double>() * (pr - 0.5)));
            break;
        }
        case VOLATILITY_MODEL::LAPLACE_DIST:{
            result = _location - (_gen_volatility[index] * sgn<double>(pr - 0.5) * log( 1 - (2 * abs(pr - 0.5)) ));
            break;
        }
        case VOLATILITY_MODEL::LOGISTIC_DIST:{
            result = _location + (_gen_volatility[index] * log(pr/(1-pr)));
            break;
        }
    }
    return result;
}

double volatility_gen::get_probability (int index) const{
    if (index==0){
        return _cum_density_array[index];
    } else {
        return (_cum_density_array[index] - _cum_density_array[(index-1)]);
    }
}

int volatility_gen::next_rand_index () const{
    double pivot = next_uniform();
    double * location = lower_bound(_cum_density_array, _cum_density_array+_model_size, pivot);
    return (location-_cum_density_array);
}

double volatility_gen::next_uniform() const{
    return (*_uniform_real_distribution)(*_rd_gen);
}

volatility_gen * volatility_gen::parse (const string & line, string & name, float & advance_pr){
    double location = 0.0;
    VOLATILITY_MODEL::enum_t distribution = VOLATILITY_MODEL::UNDEFINED;
    string frequency_sample_file;
    string volatility_sample_file;

    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("#dynamic")!=0){
                    cerr<<"[volatility_gen::parse()]\tExpecting #dynamic..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1:{
                name = token;
                break;
            }
            case 2:{
                location = boost::lexical_cast<double>(token);
                break;
            }
            case 3:{
                if (token.compare("NORMAL")==0 || token.compare("normal")==0){
                    distribution = VOLATILITY_MODEL::NORMAL_DIST;
                } else if (token.compare("CAUCHY")==0 || token.compare("cauchy")==0){
                    distribution = VOLATILITY_MODEL::CAUCHY_DIST;
                } else if (token.compare("LAPLACE")==0 || token.compare("laplace")==0){
                    distribution = VOLATILITY_MODEL::LAPLACE_DIST;
                } else if (token.compare("LOGISTIC")==0 || token.compare("logistic")==0){
                    distribution = VOLATILITY_MODEL::LOGISTIC_DIST;
                } else {
                    cerr<<"[volatility_gen::parse()]\tUnsupported distribution..."<<"\n";
                    cerr<<"                         \tExpecting one of NORMAL, CAUCHY, LAPLACE, LOGISTIC..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 4:{
                frequency_sample_file = token;
                break;
            }
            case 5:{
                volatility_sample_file = token;
                break;
            }
            case 6:{
                advance_pr = boost::lexical_cast<float>(token);
                break;
            }
        }
        index++;
    }
    if (index!=7){
        cerr<<"[volatility_gen::parse()]\tUnsupported number of arguments..."<<"\n";
        exit(0);
    }
    return new volatility_gen(frequency_sample_file.c_str(), location, distribution, volatility_sample_file.c_str());
}

void volatility_gen::test (){
    string line ("#dynamic dbpedia 0.0 NORMAL frequency.txt volatility.txt 0.025");
    //volatility_gen v_generator ("frequency.txt", 0.0, VOLATILITY_MODEL::CAUCHY_DIST, "volatility.txt");
    string name;
    float advance_pr;
    volatility_gen * v_generator = volatility_gen::parse(line, name, advance_pr);
    cout << name << ", " << advance_pr << "\n";

    int model_size = 1000;
    v_generator->initialize(model_size);

    for (int i=0; i<10000; i++){
        v_generator->advance ();
    }

    for (int j=0; j<100000; j++){
        cout << v_generator->next_rand_index() << "\n";
    }

    for (int k=0; k<model_size; k++){
        cout << k << ":" << v_generator->get_probability(k) << "\n";
    }

    delete v_generator;

    /*
    for (int i=0; i<10; i++){
        cout << v_generator.get_volatility_value(i) << " ";
        for (int j=0; j<1000; j++){
            cout << v_generator.get_next_ln_increase(i) << " ";
        }
        cout << "\n";
    }
    */
}
