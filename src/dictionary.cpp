#include "dictionary.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;

dictionary * dictionary::_instance = NULL;

dictionary::dictionary(){

}

dictionary::~dictionary(){

}

dictionary * dictionary::get_instance(){
    if (_instance==NULL){
        _instance = new dictionary();
    }
    return _instance;
}

void dictionary::destroy_instance(){
    if (_instance!=NULL){
        delete _instance;
    }
    _instance = NULL;
}

void dictionary::init (const char * words_filename, const char * firstnames_filename, const char * lastnames_filename){
    //cout<<"Initializing dictionary..."<<"\n";

    for (int i=0; i<DICTIONARY_TYPES::COUNT; i++){
        ifstream fis;
        switch (i){
            case 0:{
                fis.open(words_filename);
                break;
            }
            case 1:{
                fis.open(firstnames_filename);
                break;
            }
            case 2:{
                fis.open(lastnames_filename);
                break;
            }
        }
        string line, token;
        while (fis.good() && !fis.eof()){
            getline(fis, line);
            stringstream parser(line);
            while (parser>>token){
                for (int k=0; k<token.size(); k++){
                    if (((int)(token[k]))<32){
                        token[k] = (char) 32;
                    }
                    if (((int)(token[k]))>127){
                        token[k] = (char) 127;
                    }
                }
                _words[i].push_back(token);
            }
        }
        fis.close();
        sort(_words[i].begin(), _words[i].end());
    }

    //cout<<"Dictionary initialized..."<<"\n";
}

unsigned int dictionary::word_count (DICTIONARY_TYPES::enum_t dictionary_type){
    return _words[((int)dictionary_type)].size();
}

string * dictionary::get_word (DICTIONARY_TYPES::enum_t dictionary_type, unsigned int index){
    if (index<_words[((int)dictionary_type)].size()){
        return &(_words[((int)dictionary_type)][index]);
    } else {
        return NULL;
    }
}

pair<unsigned int, unsigned int> dictionary::get_interval (DICTIONARY_TYPES::enum_t dictionary_type, const string & range_min, const string & range_max){
    // The interval is closed on the lower-bound and open on the upper-bound...
    vector<string>::iterator low, high;
    low = lower_bound(_words[((int)dictionary_type)].begin(), _words[((int)dictionary_type)].end(), range_min);
    high = upper_bound(_words[((int)dictionary_type)].begin(), _words[((int)dictionary_type)].end(), range_max);
    return pair<unsigned int, unsigned int> (low-_words[((int)dictionary_type)].begin(), high-_words[((int)dictionary_type)].begin());
}


