#include "statistics.h"

#include <algorithm>
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>

ostream& operator<<(ostream& os, const QUERY_STRUCTURE::enum_t & query_structure){
    switch (query_structure){
        case QUERY_STRUCTURE::PATH:{
            os << "[path]";
            break;
        }
        case QUERY_STRUCTURE::STAR:{
            os << "[star]";
            break;
        }
        case QUERY_STRUCTURE::SNOWFLAKE:{
            os << "[snowflake]";
            break;
        }
        case QUERY_STRUCTURE::COMPLEX:{
            os << "[complex]";
            break;
        }
        case QUERY_STRUCTURE::UNDEFINED:{
            os << "[undefined]";
            break;
        }
    }
    return os;
}

ostream& operator<<(ostream& os, const QUERY_CATEGORY::enum_t & query_category){
    switch (query_category){
        case QUERY_CATEGORY::LOW_SELECTIVITY:{
            os << "[low]";
            break;
        }
        case QUERY_CATEGORY::MEDIUM_SELECTIVITY:{
            os << "[medium]";
            break;
        }
        case QUERY_CATEGORY::HIGH_SELECTIVITY:{
            os << "[high]";
            break;
        }
        case QUERY_CATEGORY::UNDEFINED:{
            os << "[undefined]";
            break;
        }
    }
    return os;
}

ostream& operator<<(ostream& os, const statistics_st & stats){
    os <<"\t" << "(" << stats._vertex1 << ", " << stats._edge << ", " << stats._vertex2 << ", " << stats._right_distribution << ")" << "\n";
    return os;
}

statistics_st::statistics_st (const string & vertex1, const string & vertex2, const string & edge, DISTRIBUTION_TYPES::enum_t right_distribution, int group_id){
    _vertex1 = vertex1;
    _vertex2 = vertex2;
    _edge = edge;
    _right_distribution = right_distribution;
    _group_id = group_id;
    _pr_left = 0.0;
    _pr_right = 0.0;
}

bool statistics_st::operator< (const statistics_st & rhs) const{
    if (_edge.compare(rhs._edge)!=0){
        return _edge.compare(rhs._edge)<0;
    }
    if (_vertex1.compare(rhs._vertex1)!=0){
        return _vertex1.compare(rhs._vertex1)<0;
    }
    if (_vertex2.compare(rhs._vertex2)!=0){
        return _vertex2.compare(rhs._vertex2)<0;
    }
}

statistics::statistics(const model * mdl, const vector<triple_st> & triple_array, int maxQSize, int qCount, int constCount, bool constJoinVertexAllowed, bool dupEdgesAllowed){
    srand (time(NULL));
    _model = mdl;
    index_triples(triple_array);
    extract_schema(*_model);
    //print_graph();

    ///compute();
    //print_stats();

    map<pair<QUERY_STRUCTURE::enum_t, QUERY_CATEGORY::enum_t>, map<string, string> > query_map;

    for (int i=0; i<qCount; ){
        int qSize = (rand() % maxQSize) + 1;
        if (traverse_graph(qSize, constCount, constJoinVertexAllowed, dupEdgesAllowed, query_map)){
            i++;
        }
    }

    /*
    // Randomly select a subset of queries from each category...
    cout << "Randomly selected query templates ..." << "\n";
    map<pair<QUERY_STRUCTURE::enum_t, QUERY_CATEGORY::enum_t>, map<string, string> >::iterator itr1 = query_map.begin();
    for (; itr1!=query_map.end(); itr1++){
        if (itr1->first.first != QUERY_STRUCTURE::UNDEFINED && itr1->first.second != QUERY_CATEGORY::UNDEFINED){
            cout << "CATEGORY ::: " << itr1->first.first << " - " << itr1->first.second << "\n";
            map<string, string> query_description_map = itr1->second;
            vector<string> keys;
            for (map<string, string>::iterator itr2=query_description_map.begin(); itr2!=query_description_map.end(); itr2++){
                keys.push_back(itr2->first);
            }
            for (int k=0; k<5; k++){
                string random_query = keys[rand()%keys.size()];
                string description = query_description_map[random_query];
                cout << random_query << description << "\n";
            }
        }
    }
    */
}

statistics::~statistics(){
}

void statistics::extract_schema(const model & mdl){
    vector<statistics_st> * result = new vector<statistics_st> ();
    int group_id = 0;
    for (vector<resource_m_t*>::const_iterator itr1=mdl._resource_array.begin(); itr1!=mdl._resource_array.end(); itr1++){
        const resource_m_t * rsrc = *itr1;
        for (vector<predicate_group_m_t*>::const_iterator itr2=rsrc->_predicate_group_array.begin(); itr2!=rsrc->_predicate_group_array.end(); itr2++){
            const predicate_group_m_t * pgroup = *itr2;
            for (vector<predicate_m_t*>::const_iterator itr3=pgroup->_predicate_array.begin(); itr3!=pgroup->_predicate_array.end(); itr3++){
                const predicate_m_t * pred = *itr3;
                string vertex1 = "", vertex2 = "", edge = "";
                vertex1.append(rsrc->_type_prefix);
                if (pgroup->_type_restriction!=NULL){
                    vertex1.append("@");
                    vertex1.append(*pgroup->_type_restriction);
                }
                switch (pred->_literal_type){
                    case LITERAL_TYPES::DATE:{
                        vertex2.append("date");
                        break;
                    }
                    case LITERAL_TYPES::INTEGER:{
                        vertex2.append("integer");
                        break;
                    }
                    case LITERAL_TYPES::NAME:{
                        vertex2.append("name");
                        break;
                    }
                    case LITERAL_TYPES::STRING:{
                        vertex2.append("string");
                        break;
                    }
                }
                edge.append(pred->_label);
                result->push_back(statistics_st(vertex1, vertex2, edge, pred->_distribution_type, group_id));
            }
            group_id++;
        }
    }
    for (vector<association_m_t*>::const_iterator itr1=mdl._association_array.begin(); itr1!=mdl._association_array.end(); itr1++){
        const association_m_t * assoc = *itr1;
        string vertex1 = "", vertex2 = "", edge = "";
        vertex1.append(assoc->_subject_type);
        if (assoc->_subject_type_restriction!=NULL){
            vertex1.append("@");
            vertex1.append(*assoc->_subject_type_restriction);
        }
        vertex2.append(assoc->_object_type);
        if (assoc->_object_type_restriction!=NULL){
            vertex2.append("@");
            vertex2.append(*assoc->_object_type_restriction);
        }
        edge.append(assoc->_predicate);
        result->push_back(statistics_st(vertex1, vertex2, edge, assoc->_right_distribution, group_id));
        group_id++;
    }
    populate_graph(*result);
    infer_edges();
    delete result;
}

void statistics::populate_graph(const vector<statistics_st> & tuples){
    for (vector<statistics_st>::const_iterator itr1=tuples.begin(); itr1!=tuples.end(); itr1++){
        statistics_st tuple = *itr1;
        if (graph.find(tuple._vertex1)==graph.end()){
            graph.insert(pair<string, set<statistics_st> >(tuple._vertex1, set<statistics_st>()));
        }
        graph[tuple._vertex1].insert(tuple);

//        int pos = string::npos;
//        if ( (pos = tuple._vertex1.find("@"))!=string::npos){
//            string inferred_vertex = tuple._vertex1.substr(0, pos);
//            if (graph.find(inferred_vertex)==graph.end()){
//                graph.insert(pair<string, set<statistics_st> >(inferred_vertex, set<statistics_st>()));
//            }
//            graph[inferred_vertex].insert(tuple);
//        }

        if (tuple._vertex2.compare("date") !=0 && tuple._vertex2.compare("integer") !=0 && tuple._vertex2.compare("name") !=0 && tuple._vertex2.compare("string")){
            if (graph.find(tuple._vertex2)==graph.end()){
                graph.insert(pair<string, set<statistics_st> >(tuple._vertex2, set<statistics_st>()));
            }
            graph[tuple._vertex2].insert(tuple);

//            pos = string::npos;
//            if ( (pos = tuple._vertex2.find("@"))!=string::npos){
//                string inferred_vertex = tuple._vertex2.substr(0, pos);
//                if (graph.find(inferred_vertex)==graph.end()){
//                    graph.insert(pair<string, set<statistics_st> >(inferred_vertex, set<statistics_st>()));
//                }
//                graph[inferred_vertex].insert(tuple);
//            }
        }
    }
}

void statistics::infer_edges(){
    for (map<string, set<statistics_st> >::const_iterator itr1=graph.begin(); itr1!=graph.end(); itr1++){
        string vertex = itr1->first;
        int pos = string::npos;
        if ( (pos = vertex.find("@"))!=string::npos){
            string base_vertex = vertex.substr(0, pos);
            // Insert all edges from base class...
            set<statistics_st> base_edges = graph[base_vertex];
            for (set<statistics_st>::iterator itr2=base_edges.begin(); itr2!=base_edges.end(); itr2++){
                graph[vertex].insert(*itr2);
            }
        }
    }
}

bool statistics::traverse_graph(int max_size, int const_count, bool constJoinVertexAllowed, bool dupEdgesAllowed, map<pair<QUERY_STRUCTURE::enum_t, QUERY_CATEGORY::enum_t>, map<string, string> > & query_map) const{
    vector<string> v_array;
    for (map<string, set<statistics_st> >::const_iterator itr1=graph.begin(); itr1!=graph.end(); itr1++){
        v_array.push_back(itr1->first);
    }

    string v = v_array[rand() % v_array.size()];

    set<statistics_st> traversed_edges;
    //multiset<statistics_st> traversed_edges;
    int itr_counter = 0;
    do {
        vector<statistics_st> potential_edges;
        map<string, set<statistics_st> >::const_iterator fit = graph.find(v);
        set<statistics_st> incident_edges = fit->second;
        for (set<statistics_st>::const_iterator itr1=incident_edges.begin(); itr1!=incident_edges.end(); itr1++){
            if (dupEdgesAllowed || traversed_edges.find(*itr1)==traversed_edges.end()){
                potential_edges.push_back(*itr1);
            }
        }
        if (potential_edges.empty()){
            break;
        }

        statistics_st next_edge = potential_edges[rand() % potential_edges.size()];
        //cout << "\t" << "(" << next_edge._vertex1 << ", " << next_edge._edge << ", " << next_edge._vertex2 << ")" << "\n";
        traversed_edges.insert(next_edge);

        vector<string> potential_next_vertices;
        if (graph.find(next_edge._vertex1)!=graph.end()){
            potential_next_vertices.push_back(next_edge._vertex1);
        }
        if (graph.find(next_edge._vertex2)!=graph.end()){
            potential_next_vertices.push_back(next_edge._vertex2);
        }
        v = potential_next_vertices[rand() % potential_next_vertices.size()];

        itr_counter++;
    } while (traversed_edges.size()<max_size && itr_counter<(max_size*20));

    // Compute vertices in query graph...
    set<string> variable_set;
    string query_str = "";
    int var_count = 0;
    map<string, int> q_vertex_map;
    map<string, set<string> > variable_map;
    for (set<statistics_st>::iterator itr1=traversed_edges.begin(); itr1!=traversed_edges.end(); itr1++){
        string var1 = "", var2 = "";
        string v1_base = itr1->_vertex1, v2_base = itr1->_vertex2;
        int pos = string::npos;
        if ((pos=v1_base.find("@"))!=string::npos){
            v1_base = v1_base.substr(0, pos);
        }
        if ((pos=v2_base.find("@"))!=string::npos){
            v2_base = v2_base.substr(0, pos);
        }
        if (q_vertex_map.find(v1_base)==q_vertex_map.end()){
            q_vertex_map.insert(pair<string, int>(v1_base, var_count));
            var_count++;
        }
        var1.append("?v");
        var1.append(boost::lexical_cast<string>(q_vertex_map[v1_base]));
        query_str.append("\t");
        query_str.append(var1);
        query_str.append("\t");
        query_str.append(itr1->_edge);
        query_str.append("\t");
        if (v2_base.compare("date") !=0 && v2_base.compare("integer") !=0 && v2_base.compare("name") !=0 && v2_base.compare("string")){
            if (q_vertex_map.find(v2_base)==q_vertex_map.end()){
                q_vertex_map.insert(pair<string, int>(v2_base, var_count));
                var_count++;
            }
            var2.append("?v");
            var2.append(boost::lexical_cast<string>(q_vertex_map[v2_base]));
            query_str.append(var2);
        } else {
            var2.append("?v");
            var2.append(boost::lexical_cast<string>(var_count));
            query_str.append(var2);
            var_count++;
        }
        query_str.append(" ");
        query_str.append(".");
        query_str.append("\n");
        if (variable_map.find(var1)==variable_map.end()){
            variable_map.insert(pair<string, set<string> >(var1, set<string>()));
        }
        variable_map[var1].insert(itr1->_vertex1);
        if (variable_map.find(var2)==variable_map.end()){
            variable_map.insert(pair<string, set<string> >(var2, set<string>()));
        }
        variable_map[var2].insert(itr1->_vertex2);
        variable_set.insert(var1);
        variable_set.insert(var2);
    }

    /////////////////////////////////////////////////////////////////////////////
    // Put all traversed edges in a graph...
    // When constructing the graph, be careful to ignore literals...
    // For each join vertex (i.e., vertex with more than one incident edge),
    //  compute join selectivity.
    /////////////////////////////////////////////////////////////////////////////

    var_count = 0;
    map<string, set<statistics_st> > query_graph;
    for (set<statistics_st>::iterator itr1 = traversed_edges.begin(); itr1!= traversed_edges.end(); itr1++){
        string v1_base = itr1->_vertex1, v2_base = itr1->_vertex2;
        int pos = string::npos;
        if ((pos=v1_base.find("@"))!=string::npos){
            v1_base = v1_base.substr(0, pos);
        }
        if ((pos=v2_base.find("@"))!=string::npos){
            v2_base = v2_base.substr(0, pos);
        }
        if (query_graph.find(v1_base)==query_graph.end()){
            query_graph.insert(pair<string, set<statistics_st> >(v1_base, set<statistics_st>()));
        }
        query_graph[v1_base].insert(*itr1);

        if (v2_base.compare("date") !=0 && v2_base.compare("integer") !=0 && v2_base.compare("name") !=0 && v2_base.compare("string")){
            if (query_graph.find(v2_base)==query_graph.end()){
                query_graph.insert(pair<string, set<statistics_st> >(v2_base, set<statistics_st>()));
            }
            query_graph[v2_base].insert(*itr1);
        } else {
            string tmp_id = "?v";
            tmp_id.append(boost::lexical_cast<string>(var_count));
            query_graph[tmp_id].insert(*itr1);
            var_count++;
        }
    }

    /*
    QUERY_STRUCTURE::enum_t query_structure = QUERY_STRUCTURE::UNDEFINED;
    QUERY_CATEGORY::enum_t query_category = QUERY_CATEGORY::UNDEFINED;
    double min_join_selectivity = 1.0;
    double max_join_selectivity = 1.0;
    */

    /*
    vector<int> degree_array;
    for (map<string, set<statistics_st> >::iterator itr1=query_graph.begin(); itr1!=query_graph.end(); itr1++){
        const set<statistics_st> edge_list = itr1->second;
        degree_array.push_back(edge_list.size());
        if (edge_list.size()>=2){
            set<int> correlation_set;
            for (set<statistics_st>::const_iterator itr2=edge_list.begin(); itr2!=edge_list.end(); itr2++){
                if (correlation_set.find(itr2->_group_id)==correlation_set.end()){
                    correlation_set.insert(itr2->_group_id);
                    string entity = "";
                    bool direction = true;
                    if (itr2->_vertex1.find(itr1->first)!=string::npos){
                        entity = itr2->_vertex1;
                        direction = true;
                    } else if (itr2->_vertex2.find(itr1->first)!=string::npos){
                        entity = itr2->_vertex2;
                        direction = false;
                    } else {
                        cout<<"Direction is wrong..."<<"\n";
                        cout<<"Vertex "<<itr1->first<<"\n";
                        cout<<"Edge-1 "<<itr2->_vertex1<<"\n";
                        cout<<"Edge-2 "<<itr2->_vertex2<<"\n";
                        exit(0);
                    }
                    double max_stats = 0.0;
                    double min_stats = 1.0;
                    for (int dist=0; dist<DISTRIBUTION_TYPES::UNDEFINED; dist++){
                        string key = get_key(entity, itr2->_edge, direction, (DISTRIBUTION_TYPES::enum_t) dist);
                        map<string, pair<double, double> >::const_iterator f_itr = _statistics_table.find(key);
                        pair<double, double> stats = f_itr->second;
                        //cout<<"\t"<<key<<"\t"<<stats.first<<"\n";
                        if (stats.first>max_stats){
                            max_stats = stats.first;
                        }
                        if (stats.first<min_stats){
                            min_stats = stats.first;
                        }
                    }
                    min_join_selectivity = min_join_selectivity * min_stats;
                    max_join_selectivity = max_join_selectivity * max_stats;
                }
            }
        }
    }

    sort(degree_array.begin(), degree_array.end());

    if (degree_array.size() >=3 && degree_array[0]==1 && degree_array[1]==1 && degree_array[2]==2 && degree_array[degree_array.size()-1]==2){ // Check if it is a path query...
        query_structure = QUERY_STRUCTURE::PATH;
    } else if (degree_array.size() >= 4 && degree_array[0]==1 && degree_array[degree_array.size()-2]==1 && degree_array[degree_array.size()-1]>=3){ // Check if it is a star query...
        query_structure = QUERY_STRUCTURE::STAR;
    } else if (degree_array.size() >= 6){ // Check if it is a snowflake query...
        vector<int>::iterator low_it = lower_bound(degree_array.begin(), degree_array.end(), 3);
        if ((degree_array.end()-low_it)>=2 && *(low_it-1)==1){
            query_structure = QUERY_STRUCTURE::SNOWFLAKE;
        } else {
            query_structure = QUERY_STRUCTURE::COMPLEX;
        }
    } else if (degree_array.size() >= 3){ // Check if it is a complex query...
        query_structure = QUERY_STRUCTURE::COMPLEX;
    }

    if (max_join_selectivity >= 0.25){
        query_category = QUERY_CATEGORY::HIGH_SELECTIVITY;
    } else if (max_join_selectivity < 0.25 && max_join_selectivity >= 0.1){
        query_category = QUERY_CATEGORY::MEDIUM_SELECTIVITY;
    } else if (max_join_selectivity < 0.1 && max_join_selectivity >= 0.025){
        query_category = QUERY_CATEGORY::LOW_SELECTIVITY;
    }
    */

    /*
    pair<QUERY_STRUCTURE::enum_t, QUERY_CATEGORY::enum_t> index_key (query_structure, query_category);
    if (query_map.find(index_key)==query_map.end()){
        query_map.insert(pair<pair<QUERY_STRUCTURE::enum_t, QUERY_CATEGORY::enum_t>, map<string, string> >(index_key, map<string, string>()));
    }
    if (query_map[index_key].find(query_str)==query_map[index_key].end()){
        string description = "";
        description.append("..... ..... ..... ..... ..... ..... ..... ..... ..... .....\n");
        for (map<string, set<string> >::iterator itr1=variable_map.begin(); itr1!=variable_map.end(); itr1++){
            description.append(itr1->first);
            description.append(": ");
            for (set<string>::iterator itr2=itr1->second.begin(); itr2!=itr1->second.end(); itr2++){
                description.append(*itr2);
                description.append(" ");
            }
            description.append("\n");
        }
        description.append("[join-selectivity]:\t");
        description.append("[");
        description.append(boost::lexical_cast<string>(min_join_selectivity));
        description.append(", ");
        description.append(boost::lexical_cast<string>(max_join_selectivity));
        description.append("]");
        description.append("\n");
        description.append("{ ");
        for (int i=0; i<degree_array.size(); i++){
            description.append(boost::lexical_cast<string>(degree_array[i]));
            description.append(" ");
        }
        description.append("}\n");
        stringstream ss;
        ss<<query_structure<<"-"<<query_category<<"\n";
        description.append(ss.str());
        //description.append(query_structure);
        //description.append("-");
        //description.append(query_category);
        //description.append("\n");
        description.append("..... ..... ..... ..... ..... ..... ..... ..... ..... .....\n");
        query_map[index_key].insert(pair<string, string>(query_str, description));
    */

        /// Randomly choose variables, ignore types date, integer, name or string...
        /// You should also ignore variables corresponding to join vertices...
        vector<string> eligible_list;
        for (map<string, set<string> >::iterator itr=variable_map.begin(); itr!=variable_map.end(); itr++){
            string var_type = *(itr->second.begin());
            if (var_type.compare("date")==0 || var_type.compare("integer")==0 || var_type.compare("name")==0 || var_type.compare("string")==0){
                continue;
            }
            eligible_list.push_back(itr->first);
        }
        random_shuffle(eligible_list.begin(), eligible_list.end());

        /// Select <const-count> variables...
        if (eligible_list.size()>const_count){
            eligible_list.erase(eligible_list.begin()+const_count, eligible_list.end());
        }

        bool varValid = true;
        string mapping = "";
        for (int vid=0; vid<eligible_list.size(); vid++){
            string var_name = eligible_list[vid];
            string var_root = var_name.substr(1);

            mapping.append("#mapping");
            mapping.append(" ");
            mapping.append(var_root); /// Drop preceding '?'...
            mapping.append(" ");
            mapping.append(*(variable_map[var_name].begin()));
            mapping.append(" ");
            mapping.append("uniform");
            mapping.append("\n");

            boost::algorithm::replace_all(query_str, var_name + string(" "), string("%") + var_root + string("%") + string(" "));
            boost::algorithm::replace_all(query_str, var_name + string("\t"), string("%") + var_root + string("%") + string("\t"));

            if (!constJoinVertexAllowed){
                string placeholder = string("%") + var_root + string("%");
                if (query_str.find(placeholder)!=string::npos && query_str.find(placeholder)!=query_str.rfind(placeholder)){
                    varValid = false;
                }
            }
        }

        bool qValid = false;
        string qTemplate = "";
        qTemplate.append(mapping);
        qTemplate.append("SELECT ");
        for (set<string>::iterator itr=variable_set.begin(); itr!=variable_set.end(); itr++){
            string var_name = *itr;
            if (find(eligible_list.begin(), eligible_list.end(), var_name)==eligible_list.end()){
                qTemplate.append(var_name);
                qTemplate.append(" ");
                qValid = true;
            }
        }
        qTemplate.append("WHERE {");
        qTemplate.append("\n");
        qTemplate.append(query_str);
        qTemplate.append("}");
        qTemplate.append("\n");
        qTemplate.append("#end");
        qTemplate.append("\n");

        if (varValid && qValid){
            cout << qTemplate;
            return true;
        }
    /*
    }
    */

    return false;
}

void statistics::print_graph() const{
    int vertex_counter = 0;
    cout<<"digraph rdf {"<<"\n";
    for (map<string, set<statistics_st> >::const_iterator itr1=graph.begin(); itr1!=graph.end(); itr1++){
        const set<statistics_st> edge_list = itr1->second;
        cout<< itr1->first << " " << "[" << "label=\"" << itr1->first << "\"];" << "\n";
        for (set<statistics_st>::const_iterator itr2=edge_list.begin(); itr2!=edge_list.end(); itr2++){
            // You need to handle literals separately...
            if (itr2->_vertex2.compare("date") !=0 && itr2->_vertex2.compare("integer") !=0 && itr2->_vertex2.compare("name") !=0 && itr2->_vertex2.compare("string")){
                cout << itr1->first << " -> " << itr2->_vertex2 << " " << "[" << "label=\"" << itr2->_edge << "\"];" << "\n";
                //cout << itr2->_vertex2<< " " << "[" << "label=\"" << itr2->_vertex2 << "\"];" << "\n";
            } else {
                string vertex = "v";
                vertex.append(boost::lexical_cast<string>(vertex_counter));
                cout << itr1->first << " -> " << vertex << " " << "[" << "label=\"" << itr2->_edge << "\"];" << "\n";
                vertex_counter++;
            }
        }
    }
    cout<<"}"<<"\n";
}

/*
void statistics::compute(){
    for (map<string, set<statistics_st> >::const_iterator itr1=graph.begin(); itr1!=graph.end(); itr1++){
        const set<statistics_st> edge_list = itr1->second;
        for (set<statistics_st>::const_iterator itr2=edge_list.begin(); itr2!=edge_list.end(); itr2++){
            bool direction = true;
            if (itr1->first.find(itr2->_vertex1)!=string::npos){
                direction = true;
            } else if (itr1->first.find(itr2->_vertex2)!=string::npos){
                direction = false;
            } else {
                cerr<<"Direction is wrong..."<<"\n";
                cerr<<"Vertex "<<itr1->first<<"\n";
                cerr<<"Edge-1 "<<itr2->_vertex1<<"\n";
                cerr<<"Edge-2 "<<itr2->_vertex2<<"\n";
                exit(0);
            }
            for (int d=0; d<((int)DISTRIBUTION_TYPES::UNDEFINED); d++){
                DISTRIBUTION_TYPES::enum_t distribution = (DISTRIBUTION_TYPES::enum_t) d;
                string key = get_key(itr1->first, itr2->_edge, direction, distribution);
                //cout<<"Sampling "<<key<<"..."<<"\n";
                double mean_pr = 0.0;
                double mean_card = 0.0;
                int sampling_count = 3;
                for (int k=0; k<sampling_count; k++){
                    pair<double, double> stats = sample(itr1->first, distribution, itr2->_edge, direction);
                    mean_pr += stats.first;
                    mean_card += stats.second;
                }
                mean_pr = mean_pr / ((double) sampling_count);
                mean_card = mean_card / ((double) sampling_count);
                _statistics_table.insert(pair<string, pair<double, double> >(key, pair<double, double>(mean_pr, mean_card)));
            }
        }
    }
}
*/

/*
void statistics::print_stats() const{
    for (map<string, pair<double, double> >::const_iterator itr1=_statistics_table.begin(); itr1!=_statistics_table.end(); itr1++){
        string key = itr1->first;
        pair<double, double> value = itr1->second;
        cout<<key<<"\t"<<value.first<<"\t"<<value.second<<"\n";
    }
}
*/

void statistics::index_triples(const vector<triple_st> & triple_array){
    for (int i=0; i<triple_array.size(); i++){
        _spo_index.push_back(triple_array[i]);
        _ops_index.push_back(triple_array[i]);
    }
    sort(_spo_index.begin(), _spo_index.end(), s_compare());
    sort(_ops_index.begin(), _ops_index.end(), o_compare());
}

string statistics::get_key (string entity, string predicate, bool direction, DISTRIBUTION_TYPES::enum_t distribution) const{
    string result = "";
    result.append(entity);
    result.append("#");
    result.append(predicate);
    result.append("#");
    if (direction){
        result.append("t");
    } else {
        result.append("f");
    }
    result.append("#");
    switch (distribution){
        case DISTRIBUTION_TYPES::UNIFORM:{
            result.append("uniform");
            break;
        }
        case DISTRIBUTION_TYPES::NORMAL:{
            result.append("normal");
            break;
        }
        case DISTRIBUTION_TYPES::ZIPFIAN:{
            result.append("zipfian");
            break;
        }
        case DISTRIBUTION_TYPES::UNDEFINED:{
            result.append("undefined");
            break;
        }
    }
    return result;
}

/*
pair<double, double> statistics::sample (string entity, DISTRIBUTION_TYPES::enum_t distribution, string predicate, bool direction) const{
    mapping_m_t * generator = NULL;
    int pos = string::npos;
    if ((pos=entity.find("@"))!=string::npos){
        string type = entity.substr(0, pos);
        string restriction = entity.substr(pos+1);
        generator = new mapping_m_t (string("?x"), type, restriction, distribution);
    } else {
        generator = new mapping_m_t (string("?x"), entity, distribution);
    }
    double pr_exists = 0.0;
    double mean_card = 0.0;
    unsigned int instance_count = 0;
    generator->generate(*_model, instance_count);
    int sampling_factor = instance_count * 5;
    for (int i=0; i<sampling_factor; i++){
        int cardinality = 0;
        string min_subject = "", min_predicate = "", min_object = "";
        min_predicate.append("<");
        min_predicate.append(_model->_namespace_map.replace(predicate));
        min_predicate.append(">");
        string rdf_term = generator->generate(*_model);
        if (direction){
            min_subject.append(rdf_term);
            triple_st min_range (min_subject, min_predicate, min_object);
            //cout<<"Searching for "<<min_range<<"\n";
            vector<triple_st>::const_iterator itr = lower_bound(_spo_index.begin(), _spo_index.end(), min_range, s_compare());
            for (; itr!=_spo_index.end() && itr->_subject.compare(min_subject)==0; itr++){
                //cout<<"Debug "<<*itr<<"\n";
                if (itr->_predicate.compare(min_predicate)==0){
                    cardinality++;
                }
            }
        } else {
            min_object.append(rdf_term);
            triple_st min_range (min_subject, min_predicate, min_object);
            //cout<<"Searching for "<<min_range<<"\n";
            vector<triple_st>::const_iterator itr = lower_bound(_ops_index.begin(), _ops_index.end(), min_range, o_compare());
            for (; itr!=_ops_index.end() && itr->_object.compare(min_object)==0; itr++){
                //cout<<"Debug "<<*itr<<"\n";
                if (itr->_predicate.compare(min_predicate)==0){
                    cardinality++;
                }
            }
        }
        if (cardinality>0){
            pr_exists += 1.0;
        }
        mean_card += (double) cardinality;
    }
    mean_card = mean_card / (pr_exists+0.0000000001);
    pr_exists = pr_exists / ((double) sampling_factor);
    pair<double, double> result = pair<double, double> (pr_exists, mean_card);
    return result;
    //cout << "\t" << "[" << pr_exists << ", " << mean_card << "]" << "\n";
}
*/

















