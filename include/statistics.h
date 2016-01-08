#ifndef STATISTICS_H
#define STATISTICS_H

#include "model.h"
#include <vector>
#include <string>

namespace QUERY_STRUCTURE {
    enum enum_t {
        PATH,
        STAR,
        SNOWFLAKE,
        COMPLEX,
        UNDEFINED
    };
};

namespace QUERY_CATEGORY {
    enum enum_t {
        LOW_SELECTIVITY,
        MEDIUM_SELECTIVITY,
        HIGH_SELECTIVITY,
        UNDEFINED
    };
};

ostream& operator<<(ostream& os, const QUERY_STRUCTURE::enum_t & query_structure);
ostream& operator<<(ostream& os, const QUERY_CATEGORY::enum_t & query_category);

using namespace std;

struct statistics_st {
    string _vertex1;
    string _vertex2;
    string _edge;
    DISTRIBUTION_TYPES::enum_t _right_distribution;
    int _group_id;
    double _pr_left;
    double _pr_right;

    statistics_st (const string & vertex1, const string & vertex2, const string & edge, DISTRIBUTION_TYPES::enum_t right_distribution, int group_id);
    bool operator< (const statistics_st & rhs) const;
};

ostream& operator<<(ostream& os, const statistics_st & stats);

class statistics{
    public:
        statistics(const model * mdl, const vector<triple_st> & triple_array, int maxQSize, int qCount, int constCount, bool constJoinVertexAllowed, bool dupEdgesAllowed);
        ~statistics();

        ///void compute();
        ///void print_stats() const;
    private:
        const model * _model;
        map<string, set<statistics_st> > graph;
        vector<triple_st> _spo_index;
        vector<triple_st> _ops_index;
        map<string, pair<double, double> > _statistics_table;

        void index_triples(const vector<triple_st> & triple_array);

        void extract_schema(const model & mdl);
        void populate_graph(const vector<statistics_st> & tuples);
        void infer_edges();
        void print_graph() const;

        bool traverse_graph(int max_size, int const_count, bool constJoinVertexAllowed, bool dupEdgesAllowed, map<pair<QUERY_STRUCTURE::enum_t, QUERY_CATEGORY::enum_t>, map<string, string> > & query_map) const;

        string get_key (string entity, string predicate, bool direction, DISTRIBUTION_TYPES::enum_t distribution) const;
        ///pair<double, double> sample (string entity, DISTRIBUTION_TYPES::enum_t distribution, string predicate, bool direction) const;
};

#endif // STATISTICS_H
