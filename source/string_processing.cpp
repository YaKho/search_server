#include "string_processing.h"

using namespace std;

vector<string_view> SplitIntoWords(const string_view& text) {
    vector<string_view> words;
    string_view word;
    int start_word = 0, cur_length = 0;
    for (int i = 0; i < text.size(); i++) {
        if (text[i] == ' ') {
            if (cur_length) {
                words.push_back(string_view(text.data() + start_word, cur_length));
            }
            start_word += cur_length + 1;
            cur_length = 0;
        } else {
            ++cur_length;
        }
    }
    if (cur_length) {
        words.push_back(string_view(text.data() + start_word, cur_length));
    }
    return words;
}
