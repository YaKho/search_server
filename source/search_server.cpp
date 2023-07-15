#include "search_server.h"

using namespace std;

SearchServer::SearchServer(const string_view& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {}

SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {}

void SearchServer::AddDocument(int document_id, 
                               const string_view& document, 
                               DocumentStatus status,
                               const vector<int>& ratings) {
    if (document_id < 0) {
        throw invalid_argument("id must be positive");
    }
    if (documents_.count(document_id)) {
        throw invalid_argument("id = " + to_string(document_id) + " is already exist");
    }
    documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status, string(document.begin(), document.end()) });
    documents_id_.insert(document_id);
    const vector<string_view> words = SplitIntoWordsNoStop(string_view(documents_[document_id].document_content));
    const double inv_word_count = 1.0 / words.size();
    for (const string_view& word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
}

vector<Document> SearchServer::FindTopDocuments(const string_view& raw_query, DocumentStatus status) const {
    return FindTopDocuments(
        raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
}

vector<Document> SearchServer::FindTopDocuments(const string_view& raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
    return int(documents_.size());
}

SearchServer::Matches SearchServer::MatchDocument(const string_view& raw_query, int document_id) const {
    const auto query = ParseQuery(raw_query, true);
    vector<string_view> matched_words;
    for (const string_view& word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            return { matched_words, documents_.at(document_id).status };
        }
    }
    for (const string_view& word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }
    return { matched_words, documents_.at(document_id).status };
}

SearchServer::Matches SearchServer::MatchDocument(const std::execution::parallel_policy&, 
                                                  const string_view& raw_query, 
                                                  int document_id) const {
    auto query = ParseQuery(raw_query, false);
        
    if ( any_of(execution::par, query.minus_words.cbegin(), query.minus_words.cend(),
                     [this, document_id](const auto& word) { 
                         if (word_to_document_freqs_.count(word) == 0) {
                             return 0UL;
                         }
                         return static_cast<unsigned long>(word_to_document_freqs_.at(word).count(document_id));
                     }) ) {
        return { {}, documents_.at(document_id).status };
    }    
    vector<string_view> matched_words(query.plus_words.size());
    
    auto it = copy_if(execution::par, query.plus_words.cbegin(), query.plus_words.cend(),
                 matched_words.begin(),
                 [this, document_id](const auto& word) {
                     if (word_to_document_freqs_.count(word) == 0) {
                         return 0UL;
                     }
                     return static_cast<unsigned long>(word_to_document_freqs_.at(word).count(document_id));
                 });
    sort(execution::par, matched_words.begin(), it);
    auto cit = unique(execution::par, matched_words.begin(), it);
    matched_words.resize(distance(matched_words.begin(), cit));
    
    return { matched_words, documents_.at(document_id).status };
}

SearchServer::Matches SearchServer::MatchDocument(const execution::sequenced_policy&, 
                                                  const string_view& raw_query, 
                                                  int document_id) const {
    return SearchServer::MatchDocument(raw_query, document_id);
}

set<int>::iterator SearchServer::begin() {
	return documents_id_.begin();
}

set<int>::iterator SearchServer::end() {
	return documents_id_.end();
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
	static map<string_view, double> empty_map_;
	if (document_to_word_freqs_.count(document_id) == 0) {
		return empty_map_;
	}
	return document_to_word_freqs_.at(document_id);
}

void SearchServer::RemoveDocument(int document_id) {
	if (documents_.count(document_id) == 0) {
		throw invalid_argument("document with id = " + to_string(document_id) + " does not exist");
	}
	documents_.erase(document_id);
	documents_id_.erase(document_id);
	const auto words = GetWordFrequencies(document_id);
	for (const auto& [word, freq] : words) {
		word_to_document_freqs_[word].erase(document_id);
	}
	document_to_word_freqs_.erase(document_id);
}

bool SearchServer::IsStopWord(const string_view& word) const {
    return stop_words_.count(word) > 0;
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(const string_view& text) const {
    vector<string_view> words;
    for (const string_view& word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("document cannot contain characters from 0 to 31: " + static_cast<string>(word));
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {
    bool is_minus = false;
    if (!IsValidWord(text)) {
        throw invalid_argument("query cannot contain characters from 0 to 31: " + 
                               static_cast<std::string>(text));
    }
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-') {
        throw invalid_argument("minus-word is wrong. There is only one way to set minus-word: '-minus_word'");
    }
    return {text, is_minus, IsStopWord(text)};
}

SearchServer::Query SearchServer::ParseQuery(const string_view& text, bool flag_sort) const {
    SearchServer::Query query;
    for (const string_view& word : SplitIntoWords(text)) {
        const SearchServer::QueryWord query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                query.minus_words.push_back(query_word.data);
            } else {
                query.plus_words.push_back(query_word.data);
            }
        }
    }
    if (flag_sort) {
        sort(query.plus_words.begin(), query.plus_words.end());
        sort(query.minus_words.begin(), query.minus_words.end());
        auto it = unique(query.plus_words.begin(), query.plus_words.end());
        query.plus_words.erase(it, query.plus_words.end());
        it = unique(query.minus_words.begin(), query.minus_words.end());
        query.minus_words.erase(it, query.minus_words.end());
    }
    return query;
}

double SearchServer::ComputeWordInverseDocumentFreq(const string_view& word) const {
    return std::log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

bool SearchServer::IsValidWord(const string_view& word) {
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}
