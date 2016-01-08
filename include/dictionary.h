#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <string>
#include <vector>

namespace DICTIONARY_TYPES {
    enum enum_t {
        ENGLISH_WORDS,
        FIRST_NAMES,
        LAST_NAMES,
        COUNT
    };
};

using namespace std;

class dictionary{
    public:
        ~dictionary();

        static dictionary * get_instance();
        static void destroy_instance();

        void init (const char * words_filename, const char * firstnames_filename, const char * lastnames_filename);

        unsigned int word_count (DICTIONARY_TYPES::enum_t dictionary_type);
        string * get_word (DICTIONARY_TYPES::enum_t dictionary_type, unsigned int index);
        pair<unsigned int, unsigned int> get_interval (DICTIONARY_TYPES::enum_t dictionary_type, const string & range_min, const string & range_max);

    private:
        vector<string>          _words[(int)DICTIONARY_TYPES::COUNT];
        static dictionary *     _instance;

        dictionary();
};

#endif // DICTIONARY_H
