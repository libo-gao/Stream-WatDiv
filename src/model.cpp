#include "dictionary.h"
#include "model.h"
#include "statistics.h"
#include "volatility_gen.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
///#include <set>
#include <unordered_set>
#include <sstream>
#include <stack>

#include <math.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/date_time/posix_time/posix_time_types.hpp"
#include <boost/lexical_cast.hpp>

namespace bpt = boost::posix_time;

static unsigned int MAX_LOOP_COUNTER = 50;
static int MAX_LITERAL_WORDS = 25;

static map<int,vector<double>*> zipfian_cache;

static boost::mt19937 BOOST_RND_GEN = boost::mt19937(static_cast<unsigned> (time(0)));
static boost::normal_distribution<double> BOOST_NORMAL_DIST = boost::normal_distribution<double>(0.5, (0.5/3.0));
static boost::variate_generator<boost::mt19937, boost::normal_distribution<double> > BOOST_NORMAL_DIST_GEN (BOOST_RND_GEN, BOOST_NORMAL_DIST);

ostream& operator<<(ostream& os, const DISTRIBUTION_TYPES::enum_t & distribution){
    switch (distribution){
        case DISTRIBUTION_TYPES::UNIFORM:{
            os << "uniform";
            break;
        }
        case DISTRIBUTION_TYPES::NORMAL:{
            os << "normal";
            break;
        }
        case DISTRIBUTION_TYPES::ZIPFIAN:{
            os << "zipfian";
            break;
        }
        case DISTRIBUTION_TYPES::UNDEFINED:{
            os << "undefined";
            break;
        }
    }
    return os;
}

triple_st::triple_st (const string & line){
    vector<string> result;
    string trimmed = line.substr(0, line.find_last_of("."));
    boost::trim(trimmed);
    boost::algorithm::split(result, trimmed, boost::is_any_of("\t"));
    _subject = result[0];
    _predicate = result[1];
    _object = result[2];
}

triple_st::triple_st (const string & subject, const string & predicate, const string & object){
    _subject = subject;
    _predicate = predicate;
    _object = object;
}

bool triple_st::operator== (const triple_st & rhs) const{
    return _subject.compare(rhs._subject)==0 && _predicate.compare(rhs._predicate)==0 && _object.compare(rhs._object)==0;
}

vector<triple_st> triple_st::parse_file (const char * filename){
    vector<triple_st> result;
    ifstream ifs (filename);
    string line;
    while ( getline(ifs, line) ){
        triple_st cur_triple (line);
        result.push_back(cur_triple);
        //cout << cur_triple << "\n";
    }
    ifs.close();
    return result;
}

ostream& operator<<(ostream& os, const triple_st & triple){
    os<<triple._subject<<"\t"<<triple._predicate<<"\t"<<triple._object;
    return os;
}

bool s_compare::operator() (const triple_st & lhs, const triple_st & rhs) const{
    if (lhs._subject.compare(rhs._subject)!=0){
        return lhs._subject.compare(rhs._subject) < 0;
    }
    if (lhs._predicate.compare(rhs._predicate)!=0){
        return lhs._predicate.compare(rhs._predicate) < 0;
    }
    if (lhs._object.compare(rhs._object)!=0){
        return lhs._object.compare(rhs._object) < 0;
    }
}

bool o_compare::operator() (const triple_st & lhs, const triple_st & rhs) const{
    if (lhs._object.compare(rhs._object)!=0){
        return lhs._object.compare(rhs._object) < 0;
    }
    if (lhs._predicate.compare(rhs._predicate)!=0){
        return lhs._predicate.compare(rhs._predicate) < 0;
    }
    if (lhs._subject.compare(rhs._subject)!=0){
        return lhs._subject.compare(rhs._subject) < 0;
    }
}

namespace_m_t::namespace_m_t (string token){
    unsigned int pos = token.find_first_of('=');
    if (pos!=string::npos){
        _alias = token.substr(0, pos);
        _prefix = token.substr(pos+1);
        //cout<<"namespace ["<<_alias<<"]-->["<<_prefix<<"] generated..."<<"\n";
    } else {
        cerr<<"[namespace_m_t::namespace_m_t] Expected [alias]:[prefix]..."<<"\n";
        exit(0);
    }
}

namespace_m_t * namespace_m_t::parse (const string & line){
    string namespace_declaration;

    stringstream parser(line);
    int index = 0;
    string token;

    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("#namespace")!=0){
                    cerr<<"[predicate_m_t::parse()]\tExpecting #namespace..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1:{
                namespace_declaration = token;
                break;
            }
        }
        index++;
    }

    if (index==2){
        return new namespace_m_t(namespace_declaration);
    } else {
        cerr<<"[namespace_m_t::parse()]\tExpecting 1 argument..."<<"\n";
        exit(0);
    }
}

void namespace_map::to_str (vector<string> & lines) const{
    for (map<string, string>::const_iterator itr=_index.begin(); itr!=_index.end(); itr++){
        string line = "";
        line.append(itr->first);
        line.append(" ");
        line.append(itr->second);
        lines.push_back(line);
    }
}

type_map::type_map(){

}

type_map::~type_map(){

}

void type_map::clear(){
    _index.clear();
}

void type_map::insert (const string & instance, const string & type){
    if (_index.find(type)==_index.end()){
        _index.insert(pair<string, unordered_set<string> >(type, unordered_set<string>()));
    }
    _index[type].insert(instance);
}

bool type_map::instanceof (const string & instance, const string & type) const{
    unordered_map<string, unordered_set<string> >::const_iterator f_it1 = _index.find(type);
    if (f_it1!=_index.end()){
        unordered_set<string>::const_iterator f_it2 = f_it1->second.find(instance);
        if (f_it2!=f_it1->second.end()){
            return true;
        }
    }
    return false;
}

vector<string> * type_map::get_instances (const string & entity, const string & type) const{
    //cout<<"Looking up type restriction:"<<type<<"\n";
    unordered_map<string, unordered_set<string> >::const_iterator f_it1=_index.find(type);
    if (f_it1!=_index.end()){
        vector<string> * result = new vector<string>();
        for (unordered_set<string>::const_iterator f_it2=f_it1->second.begin(); f_it2!=f_it1->second.end(); f_it2++){
            string instance = *f_it2;
            if (boost::starts_with(instance, entity)){
                result->push_back(*f_it2);
            }
        }
        return result;
    }
    //cout<<"Type restriction not found..."<<"\n";
    //cout<<"Listing contents under type_map..."<<"\n";
    //for (map<string, set<string> >::const_iterator itr1=_index.begin(); itr1!=_index.end(); itr1++){
        //cout<<"\t"<<itr1->first<<"\n";
    //}
    //cout<<"Contents listed..."<<"\n";
    return NULL;
}

void type_map::print() const{
    for (unordered_map<string, unordered_set<string> >::const_iterator itr1=_index.begin(); itr1!=_index.end(); itr1++){
        cout<<"Type:::\t"<<itr1->first<<"\n";
        for (unordered_set<string>::const_iterator itr2=itr1->second.begin(); itr2!=itr1->second.end(); itr2++){
            cout<<"\t\t\t"<<*itr2<<"\n";
        }
    }
}

void type_map::to_str (vector<string> & lines) const{
    for (unordered_map<string, unordered_set<string> >::const_iterator itr1=_index.begin(); itr1!=_index.end(); itr1++){
        string line = "";
        line.append(itr1->first);
        line.append(" ");
        for (unordered_set<string>::const_iterator itr2=itr1->second.begin(); itr2!=itr1->second.end(); itr2++){
            line.append(*itr2);
            line.append(" ");
        }
        lines.push_back(line);
    }
}

void predicate_m_t::init(string label, LITERAL_TYPES::enum_t literal_type){
    _label = label;
    _literal_type = literal_type;
    switch (_literal_type){
        case LITERAL_TYPES::INTEGER:{
            _range_min = boost::lexical_cast<string>(numeric_limits<unsigned short>::min());
            _range_max = boost::lexical_cast<string>(numeric_limits<unsigned short>::max());
            break;
        }
        case LITERAL_TYPES::STRING:
        case LITERAL_TYPES::NAME:{
            _range_min = string("A");
            _range_max = string("z");
            break;
        }
        case LITERAL_TYPES::DATE:{
            boost::posix_time::ptime cur_time (boost::posix_time::second_clock::local_time());
            boost::gregorian::date cur_date = cur_time.date();
            _range_min = string("1970-01-01");
            _range_max = boost::gregorian::to_iso_extended_string(cur_date);
            break;
        }
    }
    _distribution_type = DISTRIBUTION_TYPES::UNIFORM;
}

predicate_m_t::predicate_m_t (string label, LITERAL_TYPES::enum_t literal_type){
    init(label, literal_type);
}

predicate_m_t::predicate_m_t (string label, LITERAL_TYPES::enum_t literal_type, string range_min, string range_max){
    init(label, literal_type);
    _range_min = range_min;
    _range_max = range_max;
}

predicate_m_t::predicate_m_t (string label, LITERAL_TYPES::enum_t literal_type, string range_min, string range_max, DISTRIBUTION_TYPES::enum_t distribution_type){
    init(label, literal_type);
    _range_min = range_min;
    _range_max = range_max;
    _distribution_type = distribution_type;
}

predicate_m_t::predicate_m_t (const predicate_m_t & rhs){
    _label = rhs._label;
    _literal_type = rhs._literal_type;
    _range_min = rhs._range_min;
    _range_max = rhs._range_max;
    _distribution_type = rhs._distribution_type;
}

predicate_m_t * predicate_m_t::parse (const string & line){
    string label;
    LITERAL_TYPES::enum_t literal_type = LITERAL_TYPES::UNDEFINED;
    string range_min;
    string range_max;
    DISTRIBUTION_TYPES::enum_t distribution_type = DISTRIBUTION_TYPES::UNDEFINED;

    stringstream parser(line);
    int index = 0;
    string token;

    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("#predicate")!=0){
                    cerr<<"[predicate_m_t::parse()]\tExpecting #predicate..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1:{
                label = token;
                break;
            }
            case 2:{
                if (token.compare("integer")==0 || token.compare("INTEGER")==0){
                    literal_type = LITERAL_TYPES::INTEGER;
                } else if (token.compare("string")==0 || token.compare("STRING")==0){
                    literal_type = LITERAL_TYPES::STRING;
                } else if (token.compare("name")==0 || token.compare("NAME")==0){
                    literal_type = LITERAL_TYPES::NAME;
                } else if (token.compare("date")==0 || token.compare("DATE")==0){
                    literal_type = LITERAL_TYPES::DATE;
                }
                break;
            }
            case 3:{
                range_min = token;
                break;
            }
            case 4:{
                range_max = token;
                break;
            }
            case 5:{
                if (token.compare("uniform")==0 || token.compare("UNIFORM")==0){
                    distribution_type = DISTRIBUTION_TYPES::UNIFORM;
                } else if (token.compare("normal")==0 || token.compare("NORMAL")==0){
                    distribution_type = DISTRIBUTION_TYPES::NORMAL;
                } else if (token.compare("zipfian")==0 || token.compare("ZIPFIAN")==0){
                    distribution_type = DISTRIBUTION_TYPES::ZIPFIAN;
                }
                break;
            }
        }
        index++;
    }

    if (index==3){
        return new predicate_m_t(label, literal_type);
    } else if (index==5){
        return new predicate_m_t(label, literal_type, range_min, range_max);
    } else if (index==6){
        return new predicate_m_t(label, literal_type, range_min, range_max, distribution_type);
    } else {
        cerr<<"[predicate_m_t::parse()]\tExpecting 2, 3 or 5 arguments..."<<"\n";
        exit(0);
    }
}

string predicate_m_t::generate (const namespace_map & n_map){
    string result = "";
    string literal = model::generate_literal(_literal_type, _distribution_type, _range_min, _range_max);
    result.append("<");
    result.append(n_map.replace(_label));
    result.append(">");
    result.append("\t");
    result.append("\"");
    result.append(literal);
    result.append("\"");
    return result;
}

predicate_group_m_t::predicate_group_m_t (){
    _post_process = false;
    _gen_probability = 1.0;
    _type_restriction = NULL;
}

predicate_group_m_t::predicate_group_m_t (float gen_probability){
    _post_process = false;
    _gen_probability = gen_probability;
    _type_restriction = NULL;
}

predicate_group_m_t::predicate_group_m_t (float gen_probability, const string & type_restriction){
    _post_process = true;
    _gen_probability = gen_probability;
    _type_restriction = new string(type_restriction);
}

predicate_group_m_t::predicate_group_m_t (const predicate_group_m_t & rhs){
    _post_process = rhs._post_process;
    _gen_probability = rhs._gen_probability;
    _type_restriction = new string(*(rhs._type_restriction));
    for (unsigned int i=0; i<rhs._predicate_array.size(); i++){
        _predicate_array.push_back(new predicate_m_t(*(rhs._predicate_array[i])));
    }
}

predicate_group_m_t::~predicate_group_m_t(){
    for (unsigned int i=0; i<_predicate_array.size(); i++){
        delete _predicate_array[i];
    }
    delete _type_restriction;
}

predicate_group_m_t * predicate_group_m_t::parse (const string & line){
    float gen_probability = 1.0;
    string type_restriction;

    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("<pgroup>")!=0){
                    cerr<<"[predicate_group_m_t::parse()]\tExpecting <pgroup>..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1:{
                gen_probability = boost::lexical_cast<float>(token);
                break;
            }
            case 2:{
                if (!boost::starts_with(token, "@")){
                    cerr<<"[predicate_group_m_t::parse()]\tExpecting '@' qualifier before type restriction..."<<"\n";
                    exit(0);
                }
                type_restriction = token.substr(1);
                break;
            }
        }
        index++;
    }

    if (index==1){
        return new predicate_group_m_t();
    } else if (index==2){
        return new predicate_group_m_t(gen_probability);
    } else if (index==3){
        return new predicate_group_m_t(gen_probability, type_restriction);
    } else {
        cerr<<"[predicate_group_m_t::parse()]\tExpecting 0, 1 or 2 arguments..."<<"\n";
        exit(0);
    }
}

resource_m_t::resource_m_t (bool scalable, string type_prefix, unsigned int scaling_coefficient){
    _scalable = scalable;
    _type_prefix = type_prefix;
    _scaling_coefficient = scaling_coefficient;
}

resource_m_t::resource_m_t (const resource_m_t & rhs){
    _scalable = rhs._scalable;
    _type_prefix = rhs._type_prefix;
    _scaling_coefficient = rhs._scaling_coefficient;
    for (unsigned int i=0; i<rhs._predicate_group_array.size(); i++){
        _predicate_group_array.push_back(new predicate_group_m_t(*(rhs._predicate_group_array[i])));
    }
}

resource_m_t::~resource_m_t (){
    for (unsigned int i=0; i<_predicate_group_array.size(); i++){
        delete _predicate_group_array[i];
    }
}

void resource_m_t::generate (const namespace_map & n_map, map<string, unsigned int> & id_cursor_map){
    if (id_cursor_map.find(_type_prefix)==id_cursor_map.end()){
        id_cursor_map[_type_prefix] = 0;
    }
    for (unsigned int id=id_cursor_map[_type_prefix]; id<(id_cursor_map[_type_prefix] + _scaling_coefficient); id++){
        string subject = "";
        subject.append("<");
        subject.append(n_map.replace(_type_prefix));
        subject.append(boost::lexical_cast<string>(id));
        subject.append(">");

        for (vector<predicate_group_m_t*>::const_iterator itr2=_predicate_group_array.begin(); itr2!=_predicate_group_array.end(); itr2++){
            predicate_group_m_t * predicate_group = *itr2;
            if (!predicate_group->_post_process){
                float draw = ((float) rand())/((float)RAND_MAX);
                if (draw<=predicate_group->_gen_probability){
                    for (vector<predicate_m_t*>::const_iterator itr3=predicate_group->_predicate_array.begin(); itr3!=predicate_group->_predicate_array.end(); itr3++){
                        predicate_m_t * predicate = *itr3;
                        string triple_str = "";
                        triple_str.append(subject);
                        triple_str.append("\t");
                        triple_str.append(predicate->generate(n_map));

                        int tab1_index = triple_str.find("\t");
                        int tab2_index = triple_str.find("\t", tab1_index+1);

                        //triple_lines.push_back(triple_st(triple_str.substr(0, tab1_index), triple_str.substr((tab1_index+1), (tab2_index-tab1_index-1)), triple_str.substr(tab2_index+1)));
                        triple_st line (triple_str.substr(0, tab1_index), triple_str.substr((tab1_index+1), (tab2_index-tab1_index-1)), triple_str.substr(tab2_index+1));
                        cout<<line<<" .\n";
                    }
                }
            }
        }
    }
    id_cursor_map[_type_prefix] += _scaling_coefficient;
}

void resource_m_t::process_type_restrictions (const namespace_map & n_map, const type_map & t_map, const map<string, unsigned int> & id_cursor_map){
    unsigned int max_count = (id_cursor_map.find(_type_prefix))->second;
    for (unsigned int id=0; id<max_count; id++){
        string subject = "";
        subject.append(n_map.replace(_type_prefix));
        subject.append(boost::lexical_cast<string>(id));

        for (vector<predicate_group_m_t*>::const_iterator itr2=_predicate_group_array.begin(); itr2!=_predicate_group_array.end(); itr2++){
            predicate_group_m_t * predicate_group = *itr2;
            if (predicate_group->_post_process && t_map.instanceof(subject, n_map.replace(*(predicate_group->_type_restriction)))){
                float draw = ((float) rand())/((float)RAND_MAX);
                if (draw<=predicate_group->_gen_probability){
                    for (vector<predicate_m_t*>::const_iterator itr3=predicate_group->_predicate_array.begin(); itr3!=predicate_group->_predicate_array.end(); itr3++){
                        predicate_m_t * predicate = *itr3;
                        string triple_str = "";
                        triple_str.append("<");
                        triple_str.append(subject);
                        triple_str.append(">");
                        triple_str.append("\t");
                        triple_str.append(predicate->generate(n_map));

                        int tab1_index = triple_str.find("\t");
                        int tab2_index = triple_str.find("\t", tab1_index+1);

                        //triple_lines.push_back(triple_st(triple_str.substr(0, tab1_index), triple_str.substr((tab1_index+1), (tab2_index-tab1_index-1)), triple_str.substr(tab2_index+1)));
                        triple_st line (triple_str.substr(0, tab1_index), triple_str.substr((tab1_index+1), (tab2_index-tab1_index-1)), triple_str.substr(tab2_index+1));
                        cout<<line<<" .\n";
                    }
                }
            }
        }
    }
}

resource_m_t * resource_m_t::parse (const string & line){
    bool scalable = true;
    string type_prefix ("not_defined");
    unsigned int scaling_coefficient = 0;

    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("<type>")!=0 && token.compare("<type*>")!=0 ){
                    cerr<<"[resource_m_t::parse()]\tExpecting <type> or <type*>..."<<"\n";
                    exit(0);
                }
                if (token.compare("<type*>")==0){
                    scalable = false;
                }
                break;
            }
            case 1:{
                type_prefix = token;
                break;
            }
            case 2:{
                scaling_coefficient = boost::lexical_cast<unsigned int>(token);
                break;
            }
        }
        index++;
    }

    if (index==3){
        return new resource_m_t(scalable, type_prefix, scaling_coefficient);
    } else {
        cerr<<"[resource_m_t::parse()]\tExpecting 2 arguments..."<<"\n";
        exit(0);
    }

}

namespace_map::namespace_map(){

}

namespace_map::~namespace_map(){

}

void namespace_map::insert (const namespace_m_t & namespace_declaration){
    if (_index.find(namespace_declaration._alias)==_index.end()){
        _index.insert(pair<string, string>(namespace_declaration._alias, namespace_declaration._prefix));
    } else {
        cerr<<"[namespace_map::insert()] Warning: trying to insert an already existing namespace declaration..."<<"\n";
    }
}

void namespace_map::insert (const string & alias, const string & prefix){
    if (_index.find(alias)==_index.end()){
        _index.insert(pair<string, string>(alias, prefix));
    } else {
        cerr<<"[namespace_map::insert()] Warning: trying to insert an already existing namespace declaration..."<<"\n";
    }
}

string namespace_map::lookup (const string & alias) const{
    map<string, string>::const_iterator f_it = _index.find(alias);
    if (f_it!=_index.end()){
        return f_it->second;
    } else {
        cerr<<"[namespace_map::lookup()] Error: alias does not exist..."<<"\n";
        exit(0);
    }
}

string namespace_map::replace (const string & content) const{
    unsigned int pos = content.find_first_of(':');
    if (pos!=string::npos){
        string alias = content.substr(0, pos);
        string suffix = content.substr(pos+1);
        string result = lookup(alias);
        result.append(suffix);
        return result;
    } else {
        return content;
    }
}

void association_m_t::init (string subject_type, string predicate, string object_type){
    _post_process = false;
    _subject_type = subject_type;
    _predicate = predicate;
    _object_type = object_type;
    _left_cardinality = 1;
    _right_cardinality = 1;
    _right_cardinality_distribution = DISTRIBUTION_TYPES::UNDEFINED;
    _left_cover = 1.0;
    _right_distribution = DISTRIBUTION_TYPES::UNIFORM;
    _subject_type_restriction = NULL;
    _object_type_restriction = NULL;
}

association_m_t::association_m_t (string subject_type, string predicate, string object_type){
    init (subject_type, predicate, object_type);
}

association_m_t::association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality){
    init (subject_type, predicate, object_type);
    _left_cardinality = left_cardinality;
    _right_cardinality = right_cardinality;
}

association_m_t::association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality, float left_cover){
    init (subject_type, predicate, object_type);
    _left_cardinality = left_cardinality;
    _right_cardinality = right_cardinality;
    _left_cover = left_cover;
}

association_m_t::association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality, float left_cover, DISTRIBUTION_TYPES::enum_t right_distribution){
    init (subject_type, predicate, object_type);
    _left_cardinality = left_cardinality;
    _right_cardinality = right_cardinality;
    _left_cover = left_cover;
    _right_distribution = right_distribution;
}

association_m_t::association_m_t (
    string subject_type,
    string predicate,
    string object_type,
    unsigned int left_cardinality,
    unsigned int right_cardinality,
    float left_cover,
    DISTRIBUTION_TYPES::enum_t right_distribution,
    const string * subject_type_restriction,
    const string * object_type_restriction){
        init (subject_type, predicate, object_type);
        _post_process = true;
        _left_cardinality = left_cardinality;
        _right_cardinality = right_cardinality;
        _left_cover = left_cover;
        _right_distribution = right_distribution;
        if (subject_type_restriction!=NULL){
            _subject_type_restriction = new string(*subject_type_restriction);
        }
        if (object_type_restriction!=NULL){
            _object_type_restriction = new string(*object_type_restriction);
        }
}

association_m_t::~association_m_t (){
    delete _subject_type_restriction;
    delete _object_type_restriction;
}

void association_m_t::generate (const namespace_map & n_map, type_map & t_map, const map<string, unsigned int> & id_cursor_map){
    if (id_cursor_map.find(_subject_type)==id_cursor_map.end()){
        cerr<<"[association_m_t::parse()] Error: association cannot be defined over undefined resource '"<<_subject_type<<"'..."<<"\n";
        exit(0);
    }
    if (id_cursor_map.find(_object_type)==id_cursor_map.end()){
        cerr<<"[association_m_t::parse()] Error: association cannot be defined over undefined resource '"<<_object_type<<"'..."<<"\n";
        exit(0);
    }

    model::clear_zipfian_cache();

    if (!_post_process){
        unsigned int left_instance_count = id_cursor_map.find(_subject_type)->second;
        unsigned int right_instance_count = id_cursor_map.find(_object_type)->second;
        unordered_set<unsigned int> mapped_instances;

        boost::posix_time::ptime t1 (bpt::microsec_clock::universal_time());

        for (unsigned int left_id=0; left_id<left_instance_count; left_id++){
            float pr = ((float) rand()) / ((float) RAND_MAX);
            if (pr<=_left_cover){
                unsigned int right_size = _right_cardinality;
                if (_right_cardinality_distribution!=DISTRIBUTION_TYPES::UNDEFINED){
                    right_size = round((double) right_size * model::generate_random(_right_cardinality_distribution));
                    right_size = (right_size > _right_cardinality) ? _right_cardinality : right_size;
                }
                for (unsigned int j=0; j<right_size; j++){
                    unsigned int loop_counter = 0;
                    unsigned int right_id = 0;
                    do {
                        double r_value = model::generate_random(_right_distribution, right_instance_count);
                        right_id = round(r_value * right_instance_count);
                        right_id = (right_id>=right_instance_count) ? (right_instance_count-1) : right_id;
                        loop_counter++;
                    } while (mapped_instances.find(right_id)!=mapped_instances.end() && loop_counter<MAX_LOOP_COUNTER);
                    if (loop_counter<MAX_LOOP_COUNTER){
                        if (_left_cardinality==1){
                            mapped_instances.insert(right_id);
                        }
                        string subject(""), predicate (""), object(""), triple ("");

                        // FIXME:: You need to add replace-command...
                        subject.append(n_map.replace(_subject_type));
                        subject.append(boost::lexical_cast<string>(left_id));

                        object.append(n_map.replace(_object_type));
                        object.append(boost::lexical_cast<string>(right_id));

                        predicate.append(n_map.replace(_predicate));

                        string subject_str(""), predicate_str (""), object_str("");
                        subject_str.append("<");
                        subject_str.append(subject);
                        subject_str.append(">");

                        predicate_str.append("<");
                        predicate_str.append(predicate);
                        predicate_str.append(">");

                        object_str.append("<");
                        object_str.append(object);
                        object_str.append(">");

                        //triple_lines.push_back(triple_st(subject_str, predicate_str, object_str));
                        triple_st line (subject_str, predicate_str, object_str);
                        cout<<line<<" .\n";

                        // Save type assertions...
                        if (predicate.compare("http://www.w3.org/1999/02/22-rdf-syntax-ns#type")==0){
                            t_map.insert(subject, object);
                        }
                    } else {
                        //cout<<"[association_m_t::generate] Warning:: failed to greedily satisfy cardinality constraints..."<<"\n";
                        //cout<<"[association_m_t::generate] Ignoring association "
                            //<<_subject_type<<left_id<<"-->"
                            //<<_object_type<<right_id<<" after"<<MAX_LOOP_COUNTER<<" trials..."<<"\n";
                    }
                }
            }
        }

        boost::posix_time::ptime t2 (bpt::microsec_clock::universal_time());
        //cerr    << "[association-generation]" << " " << (t2-t1).total_microseconds() << " "
        //        << _subject_type << " " << _predicate << " " << _object_type << " "
        //        << _left_cardinality << " " << _right_cardinality << " "
        //        << "\n";
    }
}

void association_m_t::process_type_restrictions (const namespace_map & n_map, const type_map & t_map, const map<string, unsigned int> & id_cursor_map){
    if (id_cursor_map.find(_subject_type)==id_cursor_map.end()){
        cerr<<"[association_m_t::parse()] Error: association cannot be defined over undefined resource '"<<_subject_type<<"'..."<<"\n";
        exit(0);
    }
    if (id_cursor_map.find(_object_type)==id_cursor_map.end()){
        cerr<<"[association_m_t::parse()] Error: association cannot be defined over undefined resource '"<<_object_type<<"'..."<<"\n";
        exit(0);
    }


    if (_post_process){
        unsigned int left_instance_count = id_cursor_map.find(_subject_type)->second;
        vector<string> * restricted_right_instances = NULL;
        if (_object_type_restriction!=NULL){
            restricted_right_instances = t_map.get_instances(n_map.replace(_object_type), n_map.replace(*_object_type_restriction));
        } else {
            restricted_right_instances = new vector<string>();
            unsigned int right_instance_count = id_cursor_map.find(_object_type)->second;
            for (unsigned int right_id=0; right_id<right_instance_count; right_id++){
                string instance = n_map.replace(_object_type);
                instance.append(boost::lexical_cast<string>(right_id));
                restricted_right_instances->push_back(instance);
            }
        }
        if (restricted_right_instances!=NULL){
            unsigned int right_instance_count = restricted_right_instances->size();
            set<string> mapped_instances;
            for (unsigned int left_id=0; left_id<left_instance_count; left_id++){
                string subject="";
                subject.append(n_map.replace(_subject_type));
                subject.append(boost::lexical_cast<string>(left_id));
                if (_subject_type_restriction==NULL || t_map.instanceof(subject, n_map.replace(*_subject_type_restriction))){
                    float pr = ((float) rand()) / ((float) RAND_MAX);
                    if (pr<=_left_cover){
                        unsigned int right_size = _right_cardinality;
                        if (_right_cardinality_distribution!=DISTRIBUTION_TYPES::UNDEFINED){
                            right_size = round((double) right_size * model::generate_random(_right_cardinality_distribution));
                            right_size = (right_size > _right_cardinality) ? _right_cardinality : right_size;
                        }
                        for (unsigned int j=0; j<right_size; j++){
                            string predicate="", object="", triple="";
                            unsigned int loop_counter = 0;
                            do {
                                double r_value = model::generate_random(_right_distribution, right_instance_count);
                                unsigned int right_index = round(r_value * right_instance_count);
                                right_index = (right_index>=right_instance_count) ? (right_instance_count-1) : right_index;
                                object = (*restricted_right_instances)[right_index];
                                loop_counter++;
                            } while (mapped_instances.find(object)!=mapped_instances.end() && loop_counter<MAX_LOOP_COUNTER);
                            if (loop_counter<MAX_LOOP_COUNTER){
                                if (_left_cardinality==1){
                                    mapped_instances.insert(object);
                                }

                                predicate.append(n_map.replace(_predicate));

                                string subject_str = "", predicate_str = "", object_str = "";
                                subject_str.append("<");
                                subject_str.append(subject);
                                subject_str.append(">");
                                predicate_str.append("<");
                                predicate_str.append(predicate);
                                predicate_str.append(">");
                                object_str.append("<");
                                object_str.append(object);
                                object_str.append(">");

                                //triple_lines.push_back(triple_st(subject_str, predicate_str, object_str));
                                triple_st line (subject_str, predicate_str, object_str);
                                cout<<line<<" .\n";
                            }
                        }
                    }
                }
            }
            delete restricted_right_instances;
        }
    }
}

association_m_t * association_m_t::parse (const map<string, unsigned int> & id_cursor_map, const string & line){
    string subject_type ("");
    string predicate ("");
    string object_type ("");
    unsigned left_cardinality = 1;
    unsigned right_cardinality = 1;
    DISTRIBUTION_TYPES::enum_t right_cardinality_distribution = DISTRIBUTION_TYPES::UNDEFINED;
    float left_cover = 1.0;
    DISTRIBUTION_TYPES::enum_t right_distribution = DISTRIBUTION_TYPES::UNIFORM;
    string * subject_type_restriction = NULL;
    string * object_type_restriction = NULL;

    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0: {
                if (token.compare("#association")!=0){
                    cerr<<"[association_m_t::parse()]\tExpecting #association..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1: {
                subject_type = token;
                break;
            }
            case 2: {
                predicate = token;
                break;
            }
            case 3: {
                object_type = token;
                break;
            }
            case 4: {
                left_cardinality = boost::lexical_cast<unsigned int>(token);
                break;
            }
            case 5: {
                if (token.find("[uniform]")!=string::npos || token.find("[UNIFORM]")!=string::npos){
                    right_cardinality_distribution = DISTRIBUTION_TYPES::UNIFORM;
                    token = token.substr(0, token.find_first_of('['));
                } else if (token.find("[normal]")!=string::npos || token.find("[NORMAL]")!=string::npos){
                    right_cardinality_distribution = DISTRIBUTION_TYPES::NORMAL;
                    token = token.substr(0, token.find_first_of('['));
                }
                right_cardinality = boost::lexical_cast<unsigned int>(token);
                break;
            }
            case 6: {
                left_cover = boost::lexical_cast<float>(token);
                break;
            }
            case 7: {
                if (token.compare("uniform")==0 || token.compare("UNIFORM")==0){
                    right_distribution = DISTRIBUTION_TYPES::UNIFORM;
                } else if (token.compare("normal")==0 || token.compare("NORMAL")==0){
                    right_distribution = DISTRIBUTION_TYPES::NORMAL;
                } else if (token.compare("zipfian")==0 || token.compare("ZIPFIAN")==0){
                    right_distribution = DISTRIBUTION_TYPES::ZIPFIAN;
                }
                break;
            }
            case 8: {
                if (!boost::starts_with(token, "@")){
                    cerr<<"[association_m_t::parse()]\tExpecting '@' qualifier before type restriction..."<<"\n";
                    exit(0);
                }
                if (token.compare("@null")!=0 && token.compare("@NULL")!=0){
                    subject_type_restriction = new string(token.substr(1));
                }
                break;
            }
            case 9: {
                if (!boost::starts_with(token, "@")){
                    cerr<<"[association_m_t::parse()]\tExpecting '@' qualifier before type restriction..."<<"\n";
                    exit(0);
                }
                if (token.compare("@null")!=0 && token.compare("@NULL")!=0){
                    object_type_restriction = new string(token.substr(1));
                }
                break;
            }
        }
        index++;
    }
    association_m_t * result = NULL;
    if (index==4){
        result = new association_m_t (subject_type, predicate, object_type);
    } else if (index==6){
        result = new association_m_t (subject_type, predicate, object_type, left_cardinality, right_cardinality);
    } else if (index==7){
        result = new association_m_t (subject_type, predicate, object_type, left_cardinality, right_cardinality, left_cover);
    } else if (index==8){
        result = new association_m_t (subject_type, predicate, object_type, left_cardinality, right_cardinality, left_cover, right_distribution);
    } else if (index==10){
        result = new association_m_t (subject_type, predicate, object_type, left_cardinality, right_cardinality, left_cover, right_distribution, subject_type_restriction, object_type_restriction);
    } else {
        cerr<<"[association_m_t::parse()]\tExpecting 3, 5, 6, 7 or 9 arguments..."<<"\n";
        exit(0);
    }
    result->_right_cardinality_distribution = right_cardinality_distribution;
    delete subject_type_restriction;
    delete object_type_restriction;
    return result;
}

void mapping_m_t::init (const string & var_name, LITERAL_TYPES::enum_t literal_type){
    _var_name = var_name;
    _is_literal_type = true;
    _literal_type = literal_type;
    _resource_type = "";
    _type_restriction = NULL;
    _distribution_type = DISTRIBUTION_TYPES::UNIFORM;
    switch (_literal_type){
        case LITERAL_TYPES::INTEGER:{
            _range_min = boost::lexical_cast<string>(numeric_limits<unsigned short>::min());
            _range_max = boost::lexical_cast<string>(numeric_limits<unsigned short>::max());
            break;
        }
        case LITERAL_TYPES::STRING:
        case LITERAL_TYPES::NAME:{
            _range_min = string("A");
            _range_max = string("z");
            break;
        }
        case LITERAL_TYPES::DATE:{
            boost::posix_time::ptime cur_time (boost::posix_time::second_clock::local_time());
            boost::gregorian::date cur_date = cur_time.date();
            _range_min = string("1970-01-01");
            _range_max = boost::gregorian::to_iso_extended_string(cur_date);
            break;
        }
    }
    _dynamic_model_name = "";
}

void mapping_m_t::init (const string & var_name, const string & resource_type){
    _var_name = var_name;
    _is_literal_type =false;
    _literal_type = LITERAL_TYPES::UNDEFINED;
    _resource_type = resource_type;
    _type_restriction = NULL;
    _distribution_type = DISTRIBUTION_TYPES::UNIFORM;
    _range_min = "";
    _range_max = "";
    _dynamic_model_name = "";
}

mapping_m_t::mapping_m_t (const string & var_name, LITERAL_TYPES::enum_t literal_type){
    init (var_name, literal_type);
}

mapping_m_t::mapping_m_t (const string & var_name, LITERAL_TYPES::enum_t literal_type, DISTRIBUTION_TYPES::enum_t distribution_type){
    init (var_name, literal_type);
    _distribution_type = distribution_type;
}

mapping_m_t::mapping_m_t (const string & var_name, LITERAL_TYPES::enum_t literal_type, DISTRIBUTION_TYPES::enum_t distribution_type, const string & range_min, const string & range_max){
    init (var_name, literal_type);
    _distribution_type = distribution_type;
    _range_min = range_min;
    _range_max = range_max;
}

mapping_m_t::mapping_m_t (const string & var_name, const string & resource_type){
    init (var_name, resource_type);
}

mapping_m_t::mapping_m_t (const string & var_name, const string & resource_type, const string & type_restriction){
    init (var_name, resource_type);
    _type_restriction = new string (type_restriction);
}

mapping_m_t::mapping_m_t (const string & var_name, const string & resource_type, DISTRIBUTION_TYPES::enum_t distribution_type){
    init (var_name, resource_type);
    _distribution_type = distribution_type;
}

mapping_m_t::mapping_m_t (const string & var_name, const string & resource_type, const string & type_restriction, DISTRIBUTION_TYPES::enum_t distribution_type){
    init (var_name, resource_type);
    _type_restriction = new string (type_restriction);
    _distribution_type = distribution_type;
}

mapping_m_t::mapping_m_t (const mapping_m_t & rhs){
    _var_name = rhs._var_name;
    _is_literal_type = rhs._is_literal_type;
    _literal_type = rhs._literal_type;
    _resource_type = rhs._resource_type;
    _type_restriction = new string (*(rhs._type_restriction));
    _distribution_type = rhs._distribution_type;
    _range_min = rhs._range_min;
    _range_max = rhs._range_max;
    _dynamic_model_name = rhs._dynamic_model_name;
}

mapping_m_t::~mapping_m_t (){
    delete _type_restriction;
}

string mapping_m_t::generate (const model & mdl, const query_template_m_t & q_template){
    unsigned int count = 0;
    return generate(mdl, q_template, count);
}

string mapping_m_t::generate (const model & mdl, const query_template_m_t & q_template, unsigned int & instance_count){
    if (_is_literal_type){
        string result = "";
        result.append(model::generate_literal(_literal_type, _distribution_type, _range_min, _range_max));
        return result;
    } else {
        if (_type_restriction==NULL){
            string result = "";
            instance_count = mdl._id_cursor_map.find(_resource_type)->second;

            unsigned int id = 0;

            if (_distribution_type==DISTRIBUTION_TYPES::DYNAMIC){
                map<string, pair<volatility_gen*, float> >::const_iterator itr = q_template._volatility_table.find(_dynamic_model_name);
                if (itr==q_template._volatility_table.end()){
                    cerr << "[mapping_m_t::generate()]\tDynamic model " << _dynamic_model_name << " does not exist..." << "\n";
                    exit(0);
                }
                volatility_gen * v_gen = itr->second.first;
                float advance_pr = itr->second.second;
                if (!v_gen->is_initialized()){
                    v_gen->initialize(instance_count);
                }
                /*
                if ( ((float) rand() / (float) RAND_MAX) < advance_pr ){
                    v_gen->advance();
                }
                */
                int skip = ceil(1.0 / advance_pr);
                if (((q_template._instantiationCount)%skip)==0){
                    cerr << "Instantiated queries=" << q_template._instantiationCount << "\n";
                    v_gen->advance();
                }
                id = v_gen->next_rand_index();
            } else {
                double r_value = model::generate_random(_distribution_type, instance_count);
                id = round(r_value * instance_count);
            }

            id = (id>=instance_count) ? (instance_count-1) : id;
            result.append("<");
            result.append(mdl._namespace_map.replace(_resource_type));
            result.append(boost::lexical_cast<string>(id));
            result.append(">");
            return result;
        } else {
            string result = "";
            vector<string> * restricted_instances = mdl._type_map.get_instances(mdl._namespace_map.replace(_resource_type), mdl._namespace_map.replace(*_type_restriction));
            instance_count = restricted_instances->size();

            unsigned int index = 0;

            if (_distribution_type==DISTRIBUTION_TYPES::DYNAMIC){
                map<string, pair<volatility_gen*, float> >::const_iterator itr = q_template._volatility_table.find(_dynamic_model_name);
                if (itr==q_template._volatility_table.end()){
                    cerr << "[mapping_m_t::generate()]\tDynamic model " << _dynamic_model_name << " does not exist..." << "\n";
                    exit(0);
                }
                volatility_gen * v_gen = itr->second.first;
                float advance_pr = itr->second.second;
                if (!v_gen->is_initialized()){
                    v_gen->initialize(instance_count);
                }
                /*
                if ( ((float) rand() / (float) RAND_MAX) < advance_pr ){
                    v_gen->advance();
                }
                */
                int skip = ceil(1.0 / advance_pr);
                if (((q_template._instantiationCount)%skip)==0){
                    cerr << "Instantiated queries=" << q_template._instantiationCount << "\n";
                    v_gen->advance();
                }

                index = v_gen->next_rand_index();
            } else {
                double r_value = model::generate_random(_distribution_type, instance_count);
                index = round(r_value * instance_count);
            }

            index = (index>=instance_count) ? (instance_count-1) : index;
            result.append("<");
            result.append((*restricted_instances)[index]);
            result.append(">");
            delete restricted_instances;
            return result;
        }
    }
}

mapping_m_t * mapping_m_t::parse (const string & line){
    //cout<<"Parsing "<<line<<"\n";
    string var_name;
    bool is_literal_type = true;
    LITERAL_TYPES::enum_t literal_type = LITERAL_TYPES::UNDEFINED;
    string resource_type = "";
    bool type_restriction_exists = false;
    string type_restriction = "";
    DISTRIBUTION_TYPES::enum_t distribution_type = DISTRIBUTION_TYPES::UNDEFINED;
    string range_min = "";
    string range_max = "";
    string distribution_name = "";

    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("#mapping")!=0){
                    cerr<<"[mapping_m_t::parse()]\tExpecting #mapping..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1:{
                var_name = token;
                break;
            }
            case 2:{
                if (token.compare("integer")==0 || token.compare("INTEGER")==0){
                    literal_type = LITERAL_TYPES::INTEGER;
                } else if (token.compare("string")==0 || token.compare("STRING")==0){
                    literal_type = LITERAL_TYPES::STRING;
                } else if (token.compare("name")==0 || token.compare("NAME")==0){
                    literal_type = LITERAL_TYPES::NAME;
                } else if (token.compare("date")==0 || token.compare("date")==0){
                    literal_type = LITERAL_TYPES::STRING;
                } else {
                    is_literal_type = false;
                    int pos = token.find_first_of('@');
                    if (pos==string::npos){
                        resource_type = token;
                    } else {
                        resource_type = token.substr(0, pos);
                        type_restriction = token.substr(pos+1);
                        type_restriction_exists = true;
                    }
                }
                break;
            }
            case 3:{
                if (token.compare("uniform")==0 || token.compare("UNIFORM")==0){
                    distribution_type = DISTRIBUTION_TYPES::UNIFORM;
                } else if (token.compare("normal")==0 || token.compare("NORMAL")==0){
                    distribution_type = DISTRIBUTION_TYPES::NORMAL;
                } else if (token.compare("zipfian")==0 || token.compare("ZIPFIAN")==0){
                    distribution_type = DISTRIBUTION_TYPES::ZIPFIAN;
                } else if (boost::starts_with(token, "dynamic#")==0 || boost::starts_with(token, "DYNAMIC#")==0){
                    distribution_type = DISTRIBUTION_TYPES::DYNAMIC;
                    distribution_name = token.substr(8);
                }
                break;
            }
            case 4:{
                range_min = token;
                break;
            }
            case 5:{
                range_max = token;
                break;
            }
        }
        index++;
    }
    if (is_literal_type){
        if (index==3){
            return new mapping_m_t(var_name, literal_type);
        } else if (index==4){
            return new mapping_m_t(var_name, literal_type, distribution_type);
        } else if (index==6){
            return new mapping_m_t(var_name, literal_type, distribution_type, range_min, range_max);
        }
    } else {
        if (type_restriction_exists){
            if (index==3){
                return new mapping_m_t(var_name, resource_type, type_restriction);
            } else if (index==4){
                if (distribution_type==DISTRIBUTION_TYPES::DYNAMIC){
                    mapping_m_t * result = new mapping_m_t(var_name, resource_type, type_restriction, distribution_type);
                    result->_dynamic_model_name = distribution_name;
                    return result;
                } else {
                    return new mapping_m_t(var_name, resource_type, type_restriction, distribution_type);
                }
            }
        } else {
            if (index==3){
                return new mapping_m_t(var_name, resource_type);
            } else if (index==4){
                if (distribution_type==DISTRIBUTION_TYPES::DYNAMIC){
                    mapping_m_t * result = new mapping_m_t(var_name, resource_type, distribution_type);
                    result->_dynamic_model_name = distribution_name;
                    return result;
                } else {
                    return new mapping_m_t(var_name, resource_type, distribution_type);
                }
            }
        }
    }
    cerr<<"[mapping_m_t::parse()]\tIncompatible arguments..."<<"\n";
    exit(0);
}

operation_m_t::operation_m_t (const string & target_variable, const string & source_variable, OPERATION_TYPES::enum_t operation, int operand){
    _target_variable = target_variable;
    _source_variable = source_variable;
    _operation = operation;
    _operand = operand;
}

operation_m_t::operation_m_t (const operation_m_t & rhs){
    _target_variable = rhs._target_variable;
    _source_variable = rhs._source_variable;
    _operation = rhs._operation;
    _operand = rhs._operand;
}

string operation_m_t::compute (const map<string, string> & value_mappings){
    int value = boost::lexical_cast<int>(value_mappings.find(_source_variable)->second);
    if (_operation==OPERATION_TYPES::ADDITION){
        value = value + _operand;
    } else if (_operation==OPERATION_TYPES::MULTIPLICATION){
        value = value * _operand;
    } else if (_operation==OPERATION_TYPES::MOD){
        value = value % _operand;
    }
    return boost::lexical_cast<string>(value);
}

operation_m_t * operation_m_t::parse (const string & line){
    string target_variable = "", source_variable = "";
    OPERATION_TYPES::enum_t operation = OPERATION_TYPES::UNDEFINED;
    int operand = -1;
    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0:{
                if (token.compare("#operation")!=0){
                    cerr<<"[operation_m_t::parse()]\tExpecting #operation..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1:{
                target_variable = token;
                break;
            }
            case 2:{
                source_variable = token;
                break;
            }
            case 3:{
                if (token.compare("+")==0){
                    operation = OPERATION_TYPES::ADDITION;
                } else if (token.compare("*")==0){
                    operation = OPERATION_TYPES::MULTIPLICATION;
                } else if (token.compare("mod")==0){
                    operation = OPERATION_TYPES::MOD;
                }
                break;
            }
            case 4:{
                operand = boost::lexical_cast<int>(token);
                break;
            }
        }
        index++;
    }
    if (index==5){
        return new operation_m_t (target_variable, source_variable, operation, operand);
    } else {
        cerr<<"[operation_m_t::parse()]\tIncompatible number of arguments..."<<"\n";
        exit(0);
    }
}

query_template_m_t::query_template_m_t(const model * mdl){
    _mdl = mdl;
    _instantiationCount = 0;
}

query_template_m_t::query_template_m_t(const query_template_m_t & rhs){
    for (vector<mapping_m_t*>::const_iterator itr=rhs._variable_mapping_array.cbegin(); itr!=rhs._variable_mapping_array.cend(); itr++){
        mapping_m_t * mapping = *itr;
        _variable_mapping_array.push_back(new mapping_m_t(*mapping));
    }
    for (vector<operation_m_t*>::const_iterator itr=rhs._operation_array.cbegin(); itr!=rhs._operation_array.cend(); itr++){
        operation_m_t * operation = *itr;
        _operation_array.push_back(new operation_m_t(*operation));
    }
    for (map<string, pair<volatility_gen*, float> >::const_iterator itr=rhs._volatility_table.begin(); itr!=rhs._volatility_table.end(); itr++){
        string name = itr->first;
        volatility_gen * v_gen = itr->second.first;
        float advance_pr = itr->second.second;
        _volatility_table.insert(pair<string, pair<volatility_gen*,float> >(name, pair<volatility_gen*,float>(new volatility_gen(*v_gen), advance_pr)));
    }
    _template_lines.insert(_template_lines.end(), rhs._template_lines.cbegin(), rhs._template_lines.cend());
    _instantiationCount = rhs._instantiationCount;
}

query_template_m_t::~query_template_m_t(){
    for (vector<mapping_m_t*>::iterator itr=_variable_mapping_array.begin(); itr!=_variable_mapping_array.end(); itr++){
        mapping_m_t * mapping = *itr;
        delete mapping;
    }
    for (vector<operation_m_t*>::iterator itr=_operation_array.begin(); itr!=_operation_array.end(); itr++){
        operation_m_t * operation = *itr;
        delete operation;
    }
    for (map<string, pair<volatility_gen*, float> >::iterator itr=_volatility_table.begin(); itr!=_volatility_table.end(); itr++){
        volatility_gen * v_gen = itr->second.first;
        //cout << "Deleting " << itr->first << " with address " << v_gen << "\n";
        delete v_gen;
    }
}

void query_template_m_t::instantiate (unsigned int query_count, unsigned int recurrence, vector<string> & result_array){
    map<string, vector<string> > sample_map;
    unsigned int sample_count = (int)((float)query_count/(float)recurrence)+1;
    for (unsigned i=0; i<sample_count; i++){
        for (vector<mapping_m_t*>::const_iterator itr=_variable_mapping_array.cbegin(); itr!=_variable_mapping_array.cend(); itr++){
            mapping_m_t * mapping = *itr;
            if (mapping->_distribution_type!=DISTRIBUTION_TYPES::DYNAMIC){
                if (sample_map.find(mapping->_var_name)==sample_map.end()){
                    sample_map.insert(pair<string, vector<string> >(mapping->_var_name, vector<string>()));
                }
                sample_map[mapping->_var_name].push_back(mapping->generate(*_mdl, *this));
            }
        }
    }

    for (unsigned i=0; i<query_count; i++){
        string query = "";
        map<string, string> value_map;
        ///////////////////////////////////////////////////////////////////////////////////////////
        // Instead of populating value_map using mapping_m_t
        //  just sample values from sample_map...
        ///////////////////////////////////////////////////////////////////////////////////////////

        for (vector<mapping_m_t*>::const_iterator itr=_variable_mapping_array.cbegin(); itr!=_variable_mapping_array.cend(); itr++){
            mapping_m_t * mapping = *itr;
            if (mapping->_distribution_type==DISTRIBUTION_TYPES::DYNAMIC){
                value_map.insert(pair<string, string>(mapping->_var_name, mapping->generate(*_mdl, *this)));
            } else {
                vector<string> samples = sample_map[mapping->_var_name];
                value_map.insert(pair<string, string>(mapping->_var_name, samples[rand()%samples.size()]));
            }
        }

        /*
        for (map<string, vector<string> >::iterator itr=sample_map.begin(); itr!=sample_map.end(); itr++){
            vector<string> samples = itr->second;
            value_map.insert(pair<string, string>(itr->first, samples[rand()%samples.size()]));
        }
        */

        for (vector<operation_m_t*>::const_iterator itr=_operation_array.cbegin(); itr!=_operation_array.cend(); itr++){
            operation_m_t * operation = *itr;
            string value = operation->compute(value_map);
            value_map.insert(pair<string, string>(operation->_target_variable, value));
        }
        for (vector<string>::const_iterator itr=_template_lines.cbegin(); itr!=_template_lines.cend(); itr++){
            string line = *itr;
            float probability = 1.01;

            string line_cpy = line;
            boost::trim(line_cpy);
            if (line_cpy[0]=='['){
                int begin_pos = line.find_first_of("[");
                int end_pos = line.find_first_of("]");
                string line_prefix = line.substr(0, begin_pos);
                string probability_str = line.substr(begin_pos+1, end_pos-begin_pos-1);
                string line_suffix = line.substr(end_pos+1);
                probability = boost::lexical_cast<float>(probability_str);
                line = line_prefix;
                line.append(line_suffix);
            }

            /// Now you randomly generate a number, and check if it satisfies the probability.
            /// If not you do not include this triple pattern in the query...
            float random_f = ((float) rand()) / ((float) RAND_MAX);
            if (random_f > probability){
                continue;
            }

            string modified_line = "";

            // FIXME :: You need to implement a much better version of replace_all()...
            string token;
            int token_count = 0;
            stringstream tokenizer (line);
            while (tokenizer>>token){
                token_count++;
            }

            int counter = 0;
            stringstream tokenizer2 (line);
            while (tokenizer2>>token){
                if (token.find(":")!=string::npos){
                    modified_line.append("<");
                    modified_line.append(_mdl->_namespace_map.replace(token));
                    modified_line.append(">");
                } else {
                    modified_line.append(token);
                }
                counter++;
                if (counter<token_count){
                    modified_line.append(" ");
                }
            }

            while (modified_line.find('%', 0)!=string::npos){
                int begin = modified_line.find('%', 0);
                int end = modified_line.find('%', begin+1);
                string var_name = modified_line.substr(begin+1, end-begin-1);
                modified_line = modified_line.replace(begin, end-begin+1, value_map[var_name]);
            }
            if (modified_line[modified_line.size()-1]=='.'){
                query.append("\t");
            }
            query.append(modified_line);
            query.append("\n");
        }
        result_array.push_back(query);
        _instantiationCount++;
    }
}

void query_template_m_t::parse (const string filename){
    ifstream fis (filename);
    string line;
    while (fis.good() && !fis.eof()){
        getline(fis, line);
        if (fis.good() && !fis.eof()){
            if (boost::starts_with(line, "#mapping")){
                mapping_m_t * mapping = mapping_m_t::parse(line);
                _variable_mapping_array.push_back(mapping);
            } else if (boost::starts_with(line, "#operation")){
                operation_m_t * operation = operation_m_t::parse(line);
                _operation_array.push_back(operation);
            } else if (boost::starts_with(line, "#dynamic")){
                string name = "";
                float advance_pr = -1.0;
                volatility_gen * v_gen = volatility_gen::parse(line, name, advance_pr);
                if (_volatility_table.find(name)==_volatility_table.end()){
                    _volatility_table.insert(pair<string, pair<volatility_gen*, float> >(name, pair<volatility_gen*, float>(v_gen, advance_pr)));
                } else {
                    cerr << "[query_template_m_t::parse]\tIgnoring duplicate #dynamic entry..." << "\n";
                }
            } else {
                _template_lines.push_back(line);
            }
        }
    }
    fis.close();
}

void query_template_m_t::parse_str (const string & content){
    stringstream sstream (content);
    string line;
    while (getline(sstream, line, '\n')){
        if (boost::starts_with(line, "#mapping")){
            mapping_m_t * mapping = mapping_m_t::parse(line);
            _variable_mapping_array.push_back(mapping);
        } else if (boost::starts_with(line, "#operation")){
            operation_m_t * operation = operation_m_t::parse(line);
            _operation_array.push_back(operation);
        } else if (boost::starts_with(line, "#dynamic")){
                string name = "";
                float advance_pr = -1.0;
                volatility_gen * v_gen = volatility_gen::parse(line, name, advance_pr);
                if (_volatility_table.find(name)==_volatility_table.end()){
                    _volatility_table.insert(pair<string, pair<volatility_gen*, float> >(name, pair<volatility_gen*, float>(v_gen, advance_pr)));
                } else {
                    cerr << "[query_template_m_t::parse]\tIgnoring duplicate #dynamic entry..." << "\n";
                }
        } else {
            _template_lines.push_back(line);
        }
    }
}

model::model(const char * filename){
    srand (time(NULL));
    parse(filename);
}

model::~model(){
    for (vector<resource_m_t*>::iterator itr=_resource_array.begin(); itr!=_resource_array.end(); itr++){
        delete *itr;
    }
    for (vector<association_m_t*>::iterator itr=_association_array.begin(); itr!=_association_array.end(); itr++){
        delete *itr;
    }
}

void model::generate (int scale_factor){
    boost::posix_time::ptime t1 (bpt::microsec_clock::universal_time());

    for (int i=0; i<scale_factor; i++){
        for (vector<resource_m_t*>::iterator itr2=_resource_array.begin(); itr2!=_resource_array.end(); itr2++){
            resource_m_t * resource = *itr2;
            if (i==0 || resource->_scalable){
                resource->generate(_namespace_map, _id_cursor_map);
            }
        }
    }

    boost::posix_time::ptime t2 (bpt::microsec_clock::universal_time());

    for (vector<association_m_t*>::iterator itr1=_association_array.begin(); itr1!=_association_array.end(); itr1++){
        association_m_t * association = *itr1;
        association->generate(_namespace_map, _type_map, _id_cursor_map);
    }

    boost::posix_time::ptime t3 (bpt::microsec_clock::universal_time());

    for (vector<resource_m_t*>::iterator itr1=_resource_array.begin(); itr1!=_resource_array.end(); itr1++){
        resource_m_t * resource = *itr1;
        resource->process_type_restrictions(_namespace_map, _type_map, _id_cursor_map);
    }

    boost::posix_time::ptime t4 (bpt::microsec_clock::universal_time());

    for (vector<association_m_t*>::iterator itr1=_association_array.begin(); itr1!=_association_array.end(); itr1++){
        association_m_t * association = *itr1;
        association->process_type_restrictions(_namespace_map, _type_map, _id_cursor_map);
    }

    boost::posix_time::ptime t5 (bpt::microsec_clock::universal_time());

    //cerr << "[t1--t2]" << " " << (t2-t1).total_microseconds() << "\n";
    //cerr << "[t2--t3]" << " " << (t3-t2).total_microseconds() << "\n";
    //cerr << "[t3--t4]" << " " << (t4-t3).total_microseconds() << "\n";
    //cerr << "[t4--t5]" << " " << (t5-t4).total_microseconds() << "\n";
}

void model::compute_statistics (const vector<triple_st> & triples){
    vector<statistics_m_t*> statistics_array;
    for (vector<string>::iterator itr=_statistics_lines.begin(); itr!=_statistics_lines.end(); itr++){
        statistics_m_t * statistics = statistics_m_t::parse(this, *itr);
        statistics_array.push_back(statistics);
    }
    if (!statistics_array.empty()){
        for (vector<triple_st>::const_iterator itr1=triples.begin(); itr1!=triples.end(); itr1++){
            for (vector<statistics_m_t*>::iterator itr2=statistics_array.begin(); itr2!=statistics_array.end(); itr2++){
                statistics_m_t * statistics = *itr2;
                statistics->collect(this, itr1->_subject, itr1->_predicate, itr1->_object);
            }
        }
        for (vector<statistics_m_t*>::iterator itr=statistics_array.begin(); itr!=statistics_array.end(); itr++){
            statistics_m_t * statistics = *itr;
            statistics->report();
            delete statistics;
        }
    }
}

void model::parse (const char * filename){
    stack<pair<short,void*> > object_stack;
    ifstream fis (filename);
    string line;
    while (fis.good() && !fis.eof()){
        getline(fis, line);
        if (boost::starts_with(line, "//")){
            // Ignore the line, it is a comment...
        } else if (boost::starts_with(line, "#namespace")){
            namespace_m_t * namespace_declaration = namespace_m_t::parse(line);
            _namespace_map.insert(*namespace_declaration);
            delete namespace_declaration;
        } else if (boost::starts_with(line, "#predicate")){
            predicate_m_t * predicate = predicate_m_t::parse(line);
            object_stack.push(pair<short,void*>(2, (void*)predicate));
        } else if (boost::starts_with(line, "<pgroup>")){
            predicate_group_m_t * predicate_group = predicate_group_m_t::parse(line);
            object_stack.push(pair<short,void*>(1, (void*)predicate_group));
        } else if (boost::starts_with(line, "</pgroup>")){
            vector<predicate_m_t*> array;
            while (object_stack.top().first!=1){
                array.push_back((predicate_m_t * ) object_stack.top().second);
                object_stack.pop();
            }
            predicate_group_m_t * predicate_group = (predicate_group_m_t *) object_stack.top().second;
            predicate_group->_predicate_array.insert(predicate_group->_predicate_array.begin(), array.begin(), array.end());
        } else if (boost::starts_with(line, "<type")){
            resource_m_t * resource = resource_m_t::parse(line);
            object_stack.push(pair<short,void*>(0, (void*)resource));
        } else if (boost::starts_with(line, "</type>")){
            vector<predicate_group_m_t*> array;
            while (object_stack.top().first!=0){
                array.push_back((predicate_group_m_t * ) object_stack.top().second);
                object_stack.pop();
            }
            resource_m_t * resource = (resource_m_t *) object_stack.top().second;
            resource->_predicate_group_array.insert(resource->_predicate_group_array.begin(), array.begin(), array.end());
        } else if (boost::starts_with(line, "#association")){
            association_m_t * association = association_m_t::parse(_id_cursor_map, line);
            _association_array.push_back(association);
        } else if (boost::starts_with(line, "#statistics")){
            _statistics_lines.push_back(line);
        }
    }
    fis.close();
    while (!object_stack.empty()){
        _resource_array.push_back((resource_m_t *) object_stack.top().second);
        object_stack.pop();
    }
}

string model::generate_literal (LITERAL_TYPES::enum_t literal_type, DISTRIBUTION_TYPES::enum_t distribution_type, const string & range_min, const string & range_max){
    string literal = "";
    switch (literal_type){
        case LITERAL_TYPES::INTEGER:{
            int min_value = boost::lexical_cast<int>(range_min);
            int max_value = boost::lexical_cast<int>(range_max);
            int interval = max_value - min_value;
            double r_value = model::generate_random(distribution_type);
            int offset = round(r_value * interval);
            offset = (offset<0) ? 0 : offset;
            offset = (offset>interval) ? interval : offset;
            literal.append(boost::lexical_cast<string>(min_value + offset));
            break;
        }
        case LITERAL_TYPES::STRING:{
            pair<unsigned int, unsigned int> range = dictionary::get_instance()->get_interval(DICTIONARY_TYPES::ENGLISH_WORDS, range_min, range_max);
            int interval = range.second - range.first - 1;
            double r_value = model::generate_random(distribution_type);
            int offset = round(r_value * interval);
            offset = (offset<0) ? 0 : offset;
            offset = (offset>interval) ? interval : offset;
            literal.append(*(dictionary::get_instance()->get_word(DICTIONARY_TYPES::ENGLISH_WORDS, range.first + offset)));
            // Keep appending a few more words from the dictionary...
            unsigned int wc = rand() % MAX_LITERAL_WORDS;
            unsigned int dict_size = dictionary::get_instance()->word_count(DICTIONARY_TYPES::ENGLISH_WORDS);
            for (unsigned int index=0; index<wc; index++){
                literal.append(" ");
                literal.append(*(dictionary::get_instance()->get_word(DICTIONARY_TYPES::ENGLISH_WORDS, rand()%dict_size)));
            }
            break;
        }
        case LITERAL_TYPES::NAME:{
            pair<unsigned int, unsigned int> range = dictionary::get_instance()->get_interval(DICTIONARY_TYPES::FIRST_NAMES, range_min, range_max);
            int interval = range.second - range.first - 1;
            double r_value = model::generate_random(distribution_type);
            int offset = round(r_value * interval);
            offset = (offset<0) ? 0 : offset;
            offset = (offset>interval) ? interval : offset;
            literal.append(*(dictionary::get_instance()->get_word(DICTIONARY_TYPES::FIRST_NAMES, range.first + offset)));
            break;
        }
        case LITERAL_TYPES::DATE:{
            boost::posix_time::ptime min_time, max_time, gen_time;
            locale format (locale::classic(),new boost::posix_time::time_input_facet("%Y-%m-%d"));
            istringstream min_iss (range_min);
            min_iss.imbue(format);
            min_iss>>min_time;
            istringstream max_iss (range_max);
            max_iss.imbue(format);
            max_iss>>max_time;
            boost::posix_time::time_duration range (max_time - min_time);
            long interval = range.total_seconds();
            double r_value = model::generate_random(distribution_type);
            long offset = round(r_value * interval);
            offset = (offset<0) ? 0 : offset;
            offset = (offset>interval) ? interval : offset;
            gen_time = min_time + boost::gregorian::days(offset/(24*3600));
            boost::gregorian::date gen_date = gen_time.date();
            literal.append(boost::gregorian::to_iso_extended_string(gen_date));
            break;
        }
    }
    return literal;
}

double model::generate_random (DISTRIBUTION_TYPES::enum_t distribution_type, int item_count){
    double result = 0.0;
    switch (distribution_type){
        case DISTRIBUTION_TYPES::UNIFORM:{
            result = ((double) rand()) / ((double) RAND_MAX);
            break;
        }
        case DISTRIBUTION_TYPES::NORMAL:{
            result = BOOST_NORMAL_DIST_GEN();
            break;
        }
        case DISTRIBUTION_TYPES::ZIPFIAN:{
            result = generate_zipfian(item_count);
            break;
        }
        case DISTRIBUTION_TYPES::UNDEFINED:
        default:{
            break;
        }
    }
    result = (result<0.0) ? 0.0:result;
    result = (result>1.0) ? 1.0:result;
    return result;
}

double model::generate_zipfian (int item_count){
    vector<double> * intervals = NULL;

    if (zipfian_cache.find(item_count)==zipfian_cache.end()){
        intervals = new vector<double>();
        double offset = 0.0;
        for (int i=1; i<=item_count; i++){
            offset += 1.0 / ((double) (i));
            intervals->push_back(offset);
        }
        double scale_factor = 1.0 / offset;
        for (int cursor=0; cursor<item_count; cursor++){
            (*intervals)[cursor] = (*intervals)[cursor] * scale_factor;
        }
        zipfian_cache.insert(pair<int, vector<double>*>(item_count, intervals));
    } else {
        intervals = zipfian_cache[item_count];
    }

    double random_value = ((double) rand()) / ((double) RAND_MAX);
    vector<double>::iterator pivot = lower_bound(intervals->begin(), intervals->end(), random_value);
    double result = (pivot - intervals->begin()) * (1.0 / ((double) item_count));
    return result;
}

void model::clear_zipfian_cache (){
    for (map<int, vector<double>*>::iterator itr=zipfian_cache.begin(); itr!=zipfian_cache.end(); itr++){
        vector<double> * value = itr->second;
        delete value;
        itr->second = NULL;
    }
    zipfian_cache.clear();
}

void statistics_m_t::init (const model * mdl, const string & predicate, const string & subject_type, const string & object_type){
    _predicate = predicate;
    _subject_type = subject_type;
    _object_type = object_type;
    _subject_type_restriction = NULL;
    _object_type_restriction = NULL;

    _left_count = mdl->_id_cursor_map.find(subject_type)->second;
    _left_statistics = new int [_left_count];
    for (unsigned int i=0; i<_left_count; i++){
        _left_statistics[i] = 0;
    }
    _right_count = mdl->_id_cursor_map.find(object_type)->second;
    _right_statistics = new int [_right_count];
    for (unsigned int i=0; i<_right_count; i++){
        _right_statistics[i] = 0;
    }
}

statistics_m_t::statistics_m_t (const model * mdl, const string & predicate, const string & subject_type, const string & object_type){
    init (mdl, predicate, subject_type, object_type);
}

statistics_m_t::statistics_m_t (const model * mdl, const string & predicate, const string & subject_type, const string & object_type, const string * subject_type_restriction, const string * object_type_restriction){
    init (mdl, predicate, subject_type, object_type);
    if (subject_type_restriction!=NULL){
        _subject_type_restriction = new string (*subject_type_restriction);
    }
    if (object_type_restriction!=NULL){
        _object_type_restriction = new string (*object_type_restriction);
    }
}

statistics_m_t::statistics_m_t (const statistics_m_t & rhs){
    _predicate = rhs._predicate;
    _subject_type = rhs._subject_type;
    _object_type = rhs._object_type;
    if (rhs._subject_type_restriction!=NULL){
        _subject_type_restriction = new string (*rhs._subject_type_restriction);
    } else {
        _subject_type_restriction = NULL;
    }
    if (rhs._object_type_restriction!=NULL){
        _object_type_restriction = new string (*rhs._object_type_restriction);
    } else {
        _object_type_restriction = NULL;
    }
    _left_count = rhs._left_count;
    _left_statistics = new int [_left_count];
    for (unsigned int i=0; i<_left_count; i++){
        _left_statistics[i] = rhs._left_statistics[i];
    }
    _right_count = rhs._right_count;
    _right_statistics = new int [_right_count];
    for (unsigned int i=0; i<_right_count; i++){
        _right_statistics[i] = rhs._right_statistics[i];
    }
}

statistics_m_t::~statistics_m_t(){
    delete _subject_type_restriction;
    delete _object_type_restriction;
    delete [] _left_statistics;
    delete [] _right_statistics;
}

void statistics_m_t::collect (const model * mdl, const string & subject, const string & predicate, const string & object){
    string p_uri = "";
    p_uri.append("<");
    p_uri.append(mdl->_namespace_map.replace(_predicate));
    p_uri.append(">");
    if (p_uri.compare(predicate)==0){
        string s_prefix = "", o_prefix = "";
        s_prefix.append("<");
        s_prefix.append(mdl->_namespace_map.replace(_subject_type));
        o_prefix.append("<");
        o_prefix.append(mdl->_namespace_map.replace(_object_type));
        if (boost::starts_with(subject, s_prefix) && boost::starts_with(object, o_prefix)){
            // FIXME::Also check for type restrictions if they exist...
            string s_instance = subject.substr(1, subject.size()-2);
            string o_instance = object.substr(1, object.size()-2);
            unsigned int s_id = boost::lexical_cast<unsigned int>(subject.substr(s_prefix.size(), (subject.size() - s_prefix.size() - 1)));
            unsigned int o_id = boost::lexical_cast<unsigned int>(object.substr(o_prefix.size(), (object.size() - o_prefix.size() - 1)));
            if ((_subject_type_restriction==NULL || mdl->_type_map.instanceof(s_instance, mdl->_namespace_map.replace(*_subject_type_restriction))) &&
                (_object_type_restriction==NULL || mdl->_type_map.instanceof(o_instance, mdl->_namespace_map.replace(*_object_type_restriction)))){
                _left_statistics[s_id] = _left_statistics[s_id] + 1;
                _right_statistics[o_id] = _right_statistics[o_id] + 1;
            } else {
                if (!(_subject_type_restriction==NULL || mdl->_type_map.instanceof(s_instance, mdl->_namespace_map.replace(*_subject_type_restriction)))){
                    _left_statistics[s_id] = -1;
                }
                if (!(_object_type_restriction==NULL || mdl->_type_map.instanceof(o_instance, mdl->_namespace_map.replace(*_object_type_restriction)))){
                    _right_statistics[o_id] = -1;
                }
            }
        }
    }
}

void statistics_m_t::report () const{
    float left_cover = 0.0;
    unsigned int left_actual_count = 0;
    unsigned int left_min = numeric_limits<unsigned int>::max();
    unsigned int left_max = numeric_limits<unsigned int>::min();
    float left_mean = 0.0;
    vector<unsigned int> left_distribution;

    for (unsigned int i=0; i<_left_count; i++){
        if (_left_statistics[i]>0){
            left_cover += 1.0;
            if (_left_statistics[i]<left_min){
                left_min = _left_statistics[i];
            }
            if (_left_statistics[i]>left_max){
                left_max = _left_statistics[i];
            }
            left_mean += _left_statistics[i];
            left_distribution.push_back(_left_statistics[i]);
        }
        if (_left_statistics[i]>=0){
            ++left_actual_count;
        }
    }
    left_mean = left_mean / left_cover;
    left_cover = left_cover / ((float) left_actual_count);
    sort(left_distribution.begin(), left_distribution.end());

    float right_cover = 0.0;
    unsigned int right_actual_count = 0;
    unsigned int right_min = numeric_limits<unsigned int>::max();
    unsigned int right_max = numeric_limits<unsigned int>::min();
    float right_mean = 0.0;
    vector<unsigned int> right_distribution;

    for (unsigned int i=0; i<_right_count; i++){
        if (_right_statistics[i]>0){
            right_cover += 1.0;
            if (_right_statistics[i]<right_min){
                right_min = _right_statistics[i];
            }
            if (_right_statistics[i]>right_max){
                right_max = _right_statistics[i];
            }
            right_mean += _right_statistics[i];
            right_distribution.push_back(_right_statistics[i]);
        }
        if (_right_statistics[i]>=0){
            ++right_actual_count;
        }
    }
    right_mean = right_mean / right_cover;
    right_cover = right_cover / ((float) right_actual_count);
    sort(right_distribution.begin(), right_distribution.end());

    cout<<"Printing statistics..."<<"\n";
    cout<<"\tPredicate:           "<<_predicate<<"\n";
    cout<<"\tSubject-type:        "<<_subject_type<<"\n";
    cout<<"\tObject-type:         "<<_object_type<<"\n";
    if (_subject_type_restriction!=NULL){
        cout<<"\tSubject-restriction: "<<*_subject_type_restriction<<"\n";
    }
    if (_object_type_restriction!=NULL){
        cout<<"\tObject-restriction: "<<*_object_type_restriction<<"\n";
    }

    cout<<"\t\tSubject-statistics..."<<"\n";
    cout<<"\t\t\tCover:        "<<left_cover<<"\n";
    cout<<"\t\t\tRange:        "<<"["<<left_min<<"-"<<left_max<<"]"<<"\n";
    cout<<"\t\t\tMean:         "<<left_mean<<"\n";
    cout<<"\t\t\tDistribution: ";
    for (vector<unsigned int>::reverse_iterator itr=left_distribution.rbegin(); itr!=left_distribution.rend(); itr++){
        cout<<*itr<<" ";
    }
    cout<<"\n";

    cout<<"\t\tObject-statistics..."<<"\n";
    cout<<"\t\t\tCover:        "<<right_cover<<"\n";
    cout<<"\t\t\tRange:        "<<"["<<right_min<<"-"<<right_max<<"]"<<"\n";
    cout<<"\t\t\tMean:         "<<right_mean<<"\n";
    cout<<"\t\t\tDistribution: ";
    for (vector<unsigned int>::reverse_iterator itr=right_distribution.rbegin(); itr!=right_distribution.rend(); itr++){
        cout<<*itr<<" ";
    }
    cout<<"\n";
}

statistics_m_t * statistics_m_t::parse (const model * mdl, const string & line){
    string subject_type ("");
    string predicate ("");
    string object_type ("");
    string * subject_type_restriction = NULL;
    string * object_type_restriction = NULL;

    stringstream parser(line);
    int index = 0;
    string token;
    while (parser>>token){
        switch (index){
            case 0: {
                if (token.compare("#statistics")!=0){
                    cerr<<"[statistics_m_t::parse()]\tExpecting #statistics..."<<"\n";
                    exit(0);
                }
                break;
            }
            case 1: {
                subject_type = token;
                break;
            }
            case 2: {
                predicate = token;
                break;
            }
            case 3: {
                object_type = token;
                break;
            }
            case 4: {
                if (!boost::starts_with(token, "@")){
                    cerr<<"[statistics_m_t::parse()]\tExpecting '@' qualifier before type restriction..."<<"\n";
                    exit(0);
                }
                if (token.compare("@null")!=0 && token.compare("@NULL")!=0){
                    subject_type_restriction = new string(token.substr(1));
                }
                break;
            }
            case 5: {
                if (!boost::starts_with(token, "@")){
                    cerr<<"[statistics_m_t::parse()]\tExpecting '@' qualifier before type restriction..."<<"\n";
                    exit(0);
                }
                if (token.compare("@null")!=0 && token.compare("@NULL")!=0){
                    object_type_restriction = new string(token.substr(1));
                }
                break;
            }
        }
        index++;
    }
    statistics_m_t * result = NULL;
    if (index==4){
        result = new statistics_m_t (mdl, predicate, subject_type, object_type);
    } else if (index==6){
        result = new statistics_m_t (mdl, predicate, subject_type, object_type, subject_type_restriction, object_type_restriction);
    } else {
        cerr<<"[statistics_m_t::parse()]\tExpecting 3 or 5 arguments..."<<"\n";
        exit(0);
    }
    delete subject_type_restriction;
    delete object_type_restriction;
    return result;
}

void model::load (const char * filename){
    _id_cursor_map.clear();
    _type_map.clear();

    ifstream fis (filename);
    int lCount = -1;
    string line, token;

    getline(fis, line);
    lCount = boost::lexical_cast<int>(line);
    for (int i=0; i<lCount; i++){
        getline(fis, line);
        stringstream parser(line);
        parser>>token;
        string key = token;
        parser>>token;
        unsigned int value = boost::lexical_cast<unsigned int>(token);
        _id_cursor_map.insert(pair<string, unsigned int>(key, value));
    }

    getline(fis, line);
    lCount = boost::lexical_cast<int>(line);
    for (int i=0; i<lCount; i++){
        getline(fis, line);
        stringstream parser(line);
        parser>>token;
        string type = token;
        while (parser>>token){
            _type_map.insert(token, type);
        }
    }

    // You do not need to load namespaces...
    // They come automatically from the model file...
    /*
    getline(fis, line);
    lCount = boost::lexical_cast<int>(line);
    for (int i=0; i<lCount; i++){
        string key, value;
        getline(fis, line);
        stringstream parser(line);
        parser>>key;
        parser>>value;
        _namespace_map.insert(key, value);
    }
    */

    fis.close();
}

void model::save (const char * filename) const{
    ofstream fos (filename);

    fos<<_id_cursor_map.size()<<"\n";
    for (map<string, unsigned int>::const_iterator itr1=_id_cursor_map.cbegin(); itr1!=_id_cursor_map.cend(); itr1++){
        fos<<itr1->first<<" "<<boost::lexical_cast<string>(itr1->second)<<"\n";
    }

    vector<string> lines;
    _type_map.to_str(lines);
    fos<<lines.size()<<"\n";
    for (vector<string>::iterator itr1=lines.begin(); itr1!=lines.end(); itr1++){
        fos<<*itr1<<"\n";
    }

    // You do not need to save namespaces...
    // They come automatically from the model file...
    /*
    lines.clear();
    _namespace_map.to_str(lines);
    fos<<lines.size()<<"\n";
    for (vector<string>::iterator itr1=lines.begin(); itr1!=lines.end(); itr1++){
        fos<<*itr1<<"\n";
    }
    */

    fos.close();
}

int main(int argc, const char* argv[]) {
    dictionary * dict = dictionary::get_instance();
    if ( (argc==2 || argc==4 || argc==5 || argc>=6) && argv[1][0]=='-'){
        dict->init("/usr/share/dict/words", "../../files/firstnames.txt", "../../files/lastnames.txt");
        const char * model_filename = argv[2];
        model cur_model (model_filename);
        //statistics stat (cur_model);
        if (argc==4 && argv[1][0]=='-' && argv[1][1]=='d'){
            unsigned int scale_factor = boost::lexical_cast<unsigned int>(string(argv[3]));
            cur_model.generate(scale_factor);
            cur_model.save("saved.txt");
            //statistics stat (&cur_model, triples);
            dictionary::destroy_instance();
            return 0;
        } else if (argc==5 && argv[1][0]=='-' && argv[1][1]=='q'){
            cur_model.load("saved.txt");
            unsigned int query_count = boost::lexical_cast<unsigned int>(string(argv[(argc-2)]));
            unsigned int recurrence_factor = boost::lexical_cast<unsigned int>(string(argv[(argc-1)]));
            vector<string> workload;

            string line, qTemplateStr = "";
            while (getline(cin, line)){
                if (boost::starts_with(line, "#end")){
                    query_template_m_t q_template (&cur_model);
                    q_template.parse_str(qTemplateStr);
                    q_template.instantiate(query_count, recurrence_factor, workload);
                    qTemplateStr = "";
                } else {
                    qTemplateStr.append(line);
                    qTemplateStr.append("\n");
                }
            }

            // obtain a time-based seed:
            //unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
            //shuffle (workload.begin(), workload.end(), std::default_random_engine(seed));

            for (int qid=0; qid<workload.size(); qid++){
                cout<<workload[qid];
            }
            dictionary::destroy_instance();
            return 0;
        } else if (argc>=6 && argv[1][0]=='-' && argv[1][1]=='q'){
            cur_model.load("saved.txt");
            unsigned int query_count = boost::lexical_cast<unsigned int>(string(argv[(argc-2)]));
            unsigned int recurrence_factor = boost::lexical_cast<unsigned int>(string(argv[(argc-1)]));
            vector<string> workload;
            for (int template_id=3; template_id<(argc-2); template_id++){
                const char * query_filename = argv[template_id];
                query_template_m_t q_template (&cur_model);
                q_template.parse(query_filename);
                q_template.instantiate(query_count, recurrence_factor, workload);
            }

            /// obtain a time-based seed:
            //unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
            //shuffle (workload.begin(), workload.end(), std::default_random_engine(seed));

            for (int qid=0; qid<workload.size(); qid++){
                cout<<workload[qid];
            }
            dictionary::destroy_instance();
            return 0;
        } else if (argc==6 && argv[1][0]=='-' && argv[1][1]=='s'){
            cur_model.load("saved.txt");
            vector<triple_st> triple_array = triple_st::parse_file(argv[3]);
            int maxQSize = boost::lexical_cast<int>(argv[4]);
            int qCount = boost::lexical_cast<int>(argv[5]);
            statistics stat (&cur_model, triple_array, maxQSize, qCount, 1, false, false);
            dictionary::destroy_instance();
            return 0;
        } else if (argc==7 && argv[1][0]=='-' && argv[1][1]=='s'){
            cur_model.load("saved.txt");
            vector<triple_st> triple_array = triple_st::parse_file(argv[3]);
            int maxQSize = boost::lexical_cast<int>(argv[4]);
            int qCount = boost::lexical_cast<int>(argv[5]);
            int constCount = boost::lexical_cast<int>(argv[6]);
            statistics stat (&cur_model, triple_array, maxQSize, qCount, constCount, false, false);
            dictionary::destroy_instance();
            return 0;
        } else if (argc==8 && argv[1][0]=='-' && argv[1][1]=='s'){
            cur_model.load("saved.txt");
            vector<triple_st> triple_array = triple_st::parse_file(argv[3]);
            int maxQSize = boost::lexical_cast<int>(argv[4]);
            int qCount = boost::lexical_cast<int>(argv[5]);
            int constCount = boost::lexical_cast<int>(argv[6]);
            statistics stat (&cur_model, triple_array, maxQSize, qCount, constCount, argv[7][0]=='t', false);
            dictionary::destroy_instance();
            return 0;
        } else if (argc==9 && argv[1][0]=='-' && argv[1][1]=='s'){
            cur_model.load("saved.txt");
            vector<triple_st> triple_array = triple_st::parse_file(argv[3]);
            int maxQSize = boost::lexical_cast<int>(argv[4]);
            int qCount = boost::lexical_cast<int>(argv[5]);
            int constCount = boost::lexical_cast<int>(argv[6]);
            statistics stat (&cur_model, triple_array, maxQSize, qCount, constCount, argv[7][0]=='t', argv[8][0]=='t');
            dictionary::destroy_instance();
            return 0;
        } else if (argc==2 && argv[1][0]=='-' && argv[1][1]=='x'){
            volatility_gen::test();
            dictionary::destroy_instance();
            return 0;
        }
    }
    cout<<"Usage:::\t./watdiv -d <model-file> <scale-factor>"<<"\n";
    cout<<"Usage:::\t./watdiv -q <model-file> <query-count> <recurrence-factor>"<<"\n";
    cout<<"        \t./watdiv -q <model-file> <query-file> <query-count> <recurrence-factor>"<<"\n";
    cout<<"Usage:::\t./watdiv -s <model-file> <dataset-file> <max-query-size> <query-count>"<<"\n";
    cout<<"        \t./watdiv -s <model-file> <dataset-file> <max-query-size> <query-count> <constant-per-query-count>"<<"\n";
    cout<<"        \t./watdiv -s <model-file> <dataset-file> <max-query-size> <query-count> <constant-per-query-count> <constant-join-vertex-allowed?>"<<"\n";
    //cout<<"Usage:::\t./watdiv -x"<<"\n";
    //cout<<"        \t./watdiv -s <model-file> <dataset-file> <max-query-size> <query-count> <constant-per-query-count> <constant-join-vertex-allowed?> <duplicate-edges-allowed?>"<<"\n";
    dictionary::destroy_instance();
    return 0;
}
