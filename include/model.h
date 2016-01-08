#ifndef MODEL_H
#define MODEL_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <unordered_map>
#include <unordered_set>

#include <iostream>

#include <boost/random.hpp>

class volatility_gen;

using namespace std;

namespace LITERAL_TYPES {
    enum enum_t {
        INTEGER,
        STRING,
        NAME,
        DATE,
        UNDEFINED
    };
};

namespace DISTRIBUTION_TYPES {
    enum enum_t {
        UNIFORM,
        NORMAL,
        ZIPFIAN,
        DYNAMIC,
        UNDEFINED
    };
};

ostream& operator<<(ostream& os, const DISTRIBUTION_TYPES::enum_t & distribution);

namespace OPERATION_TYPES {
    enum enum_t {
        ADDITION,
        MULTIPLICATION,
        MOD,
        UNDEFINED
    };
};

// Forward declaration
struct model;

struct triple_st {
    string _subject;
    string _predicate;
    string _object;

    triple_st (const string & line);
    triple_st (const string & subject, const string & predicate, const string & object);
    bool operator== (const triple_st & rhs) const;

    static vector<triple_st> parse_file (const char * filename);
};

ostream& operator<<(ostream& os, const triple_st & triple);

struct s_compare {
    bool operator() (const triple_st & lhs, const triple_st & rhs) const;
};

struct o_compare {
    bool operator() (const triple_st & lhs, const triple_st & rhs) const;
};

struct namespace_m_t {
    string                          _alias;
    string                          _prefix;

    namespace_m_t (string token);

    static namespace_m_t * parse (const string & line);
};

class namespace_map {
    public:
        namespace_map();
        ~namespace_map();

        void insert (const namespace_m_t & namespace_declaration);
        void insert (const string & alias, const string & prefix);
        string lookup (const string & alias) const;
        string replace (const string & content) const;

        void to_str (vector<string> & lines) const;
    private:
        map<string, string> _index;
};

class type_map {
    public:
        type_map();
        ~type_map();

        void clear();
        void insert (const string & instance, const string & type);
        bool instanceof (const string & instance, const string & type) const;
        vector<string> * get_instances (const string & entity, const string & type) const;

        void print () const;
        void to_str (vector<string> & lines) const;
    private:
        /// map<string, set<string> > _index;
        unordered_map<string, unordered_set<string> > _index;
};

struct predicate_m_t {
    string                          _label;
    LITERAL_TYPES::enum_t           _literal_type;
    string                          _range_min;
    string                          _range_max;
    DISTRIBUTION_TYPES::enum_t      _distribution_type;

    void init (string label, LITERAL_TYPES::enum_t literal_type);

    predicate_m_t (string label, LITERAL_TYPES::enum_t literal_type);
    predicate_m_t (string label, LITERAL_TYPES::enum_t literal_type, string range_min, string range_max);
    predicate_m_t (string label, LITERAL_TYPES::enum_t literal_type, string range_min, string range_max, DISTRIBUTION_TYPES::enum_t distribution_type);
    predicate_m_t (const predicate_m_t & rhs);

    string generate (const namespace_map & n_map);

    static predicate_m_t * parse (const string & line);
};

struct predicate_group_m_t {
    bool                            _post_process;
    float                           _gen_probability;
    string *                        _type_restriction;
    vector<predicate_m_t*>          _predicate_array;

    predicate_group_m_t ();
    predicate_group_m_t (float gen_probability);
    predicate_group_m_t (float gen_probability, const string & type_restriction);
    predicate_group_m_t (const predicate_group_m_t & rhs);
    ~predicate_group_m_t();

    static predicate_group_m_t * parse (const string & line);
};

struct resource_m_t {
    bool                            _scalable;
    string                          _type_prefix;
    unsigned int                    _scaling_coefficient;
    vector<predicate_group_m_t*>    _predicate_group_array;

    resource_m_t (bool scalable, string type_prefix, unsigned int scaling_coefficient);
    resource_m_t (const resource_m_t & rhs);
    ~resource_m_t ();

    void generate (const namespace_map & n_map, map<string, unsigned int> & id_cursor_map);
    void process_type_restrictions (const namespace_map & n_map, const type_map & t_map, const map<string, unsigned int> & id_cursor_map);

    static resource_m_t * parse (const string & line);
};

struct association_m_t {
    bool                            _post_process;

    string                          _subject_type;
    string                          _predicate;
    string                          _object_type;

    string *                        _subject_type_restriction;
    string *                        _object_type_restriction;

    unsigned int                    _left_cardinality;
    unsigned int                    _right_cardinality;
    DISTRIBUTION_TYPES::enum_t      _right_cardinality_distribution;

    float                           _left_cover;
    DISTRIBUTION_TYPES::enum_t      _right_distribution;

    void init (string subject_type, string predicate, string object_type);

    association_m_t (string subject_type, string predicate, string object_type);
    association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality);
    association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality, float left_cover);
    association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality, float left_cover, DISTRIBUTION_TYPES::enum_t right_distribution);
    association_m_t (string subject_type, string predicate, string object_type, unsigned int left_cardinality, unsigned int right_cardinality, float left_cover, DISTRIBUTION_TYPES::enum_t right_distribution, const string * subject_type_restriction, const string * object_type_restriction);
    ~association_m_t ();

    void generate (const namespace_map & n_map, type_map & t_map, const map<string, unsigned int> & id_cursor_map);
    void process_type_restrictions (const namespace_map & n_map, const type_map & t_map, const map<string, unsigned int> & id_cursor_map);

    static association_m_t * parse (const map<string, unsigned int> & id_cursor_map, const string & line);
};

struct query_template_m_t;

struct mapping_m_t {
    string                      _var_name;
    bool                        _is_literal_type;
    LITERAL_TYPES::enum_t       _literal_type;
    string                      _resource_type;
    string *                     _type_restriction;
    DISTRIBUTION_TYPES::enum_t  _distribution_type;
    string                      _range_min;
    string                      _range_max;
    string                      _dynamic_model_name;

    void init (const string & var_name, LITERAL_TYPES::enum_t literal_type);
    void init (const string & var_name, const string & resource_type);

    mapping_m_t (const string & var_name, LITERAL_TYPES::enum_t literal_type);
    mapping_m_t (const string & var_name, LITERAL_TYPES::enum_t literal_type, DISTRIBUTION_TYPES::enum_t distribution_type);
    mapping_m_t (const string & var_name, LITERAL_TYPES::enum_t literal_type, DISTRIBUTION_TYPES::enum_t distribution_type, const string & range_min, const string & range_max);
    mapping_m_t (const string & var_name, const string & resource_type);
    mapping_m_t (const string & var_name, const string & resource_type, const string & type_restriction);
    mapping_m_t (const string & var_name, const string & resource_type, DISTRIBUTION_TYPES::enum_t distribution_type);
    mapping_m_t (const string & var_name, const string & resource_type, const string & type_restriction, DISTRIBUTION_TYPES::enum_t distribution_type);

    mapping_m_t (const mapping_m_t & rhs);
    ~mapping_m_t ();

    string generate (const model & mdl, const query_template_m_t & q_template);
    string generate (const model & mdl, const query_template_m_t & q_template, unsigned int & instance_count);

    static mapping_m_t * parse (const string & line);
};

struct operation_m_t {
    string                      _target_variable;
    string                      _source_variable;
    OPERATION_TYPES::enum_t     _operation;
    int                         _operand;

    operation_m_t (const string & target_variable, const string & source_variable, OPERATION_TYPES::enum_t operation, int operand);
    operation_m_t (const operation_m_t & rhs);

    string compute (const map<string, string> & value_mappings);

    static operation_m_t * parse (const string & line);
};

struct query_template_m_t {
    const model *                              _mdl;
    vector<mapping_m_t*>                        _variable_mapping_array;
    vector<operation_m_t*>                      _operation_array;
    vector<string>                              _template_lines;
    map<string, pair<volatility_gen*, float> >  _volatility_table;
    int                                         _instantiationCount;

    query_template_m_t(const model * mdl);
    query_template_m_t(const query_template_m_t & rhs);
    ~query_template_m_t();

    void instantiate (unsigned int query_count, unsigned int recurrence, vector<string> & result_array);

    void parse (const string filename);
    void parse_str (const string & content);
};

struct statistics_m_t {
    string              _predicate;
    string              _subject_type;
    string              _object_type;
    string *            _subject_type_restriction;
    string *            _object_type_restriction;
    unsigned int        _left_count;
    int *               _left_statistics;
    unsigned int        _right_count;
    int *               _right_statistics;

    void init (const model * mdl, const string & predicate, const string & subject_type, const string & object_type);
    statistics_m_t (const model * mdl, const string & predicate, const string & subject_type, const string & object_type);
    statistics_m_t (const model * mdl, const string & predicate, const string & subject_type, const string & object_type, const string * subject_type_restriction, const string * object_type_restriction);
    statistics_m_t (const statistics_m_t & rhs);
    ~statistics_m_t();

    void collect (const model * mdl, const string & subject, const string & predicate, const string & object);
    void report () const;

    static statistics_m_t * parse (const model * mdl, const string & line);
};

struct model{
    vector<resource_m_t*>       _resource_array;
    vector<association_m_t*>    _association_array;
    vector<string>              _statistics_lines;
    map<string, unsigned int>   _id_cursor_map;
    namespace_map               _namespace_map;
    type_map                    _type_map;

    model(const char * filename);
    ~model();

    void parse (const char * filename);

    void generate (int scale_factor);
    void compute_statistics (const vector<triple_st> & triples);

    void load (const char * filename);
    void save (const char * filename) const;

    static string generate_literal (LITERAL_TYPES::enum_t literal_type, DISTRIBUTION_TYPES::enum_t distribution_type, const string & range_min, const string & range_max);
    static double generate_random (DISTRIBUTION_TYPES::enum_t distribution_type, int item_count=-1);
    static double generate_zipfian (int item_count);
    static void clear_zipfian_cache ();
};

#endif // MODEL_H
