#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <map>
#include <set>
#include <cmath>
#include <utility>
#include <execution>
#include <optional>

#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double MERROR = 1e-6;
const int THREAD_COUNT = 32;

class SearchServer {
public:
    using Matches = std::tuple<std::vector<std::string_view>, DocumentStatus>;

    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string_view& stop_words_text);    
    explicit SearchServer(const std::string& stop_words_text);

    void AddDocument(int document_id, const std::string_view& document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename DocumentPredicate, typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, const std::string_view& raw_query, DocumentPredicate document_predicate) const;
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, const std::string_view& raw_query, DocumentStatus status) const;
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy, const std::string_view& raw_query) const;
    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(const std::string_view& raw_query, DocumentPredicate document_predicate) const;
    std::vector<Document> FindTopDocuments(const std::string_view& raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(const std::string_view& raw_query) const;

    int GetDocumentCount() const;

    Matches MatchDocument(const std::string_view& raw_query, int document_id) const;
    Matches MatchDocument(const std::execution::sequenced_policy&, const std::string_view& raw_query, int document_id) const;    
    Matches MatchDocument(const std::execution::parallel_policy&, const std::string_view& raw_query, int document_id) const;

    std::set<int>::iterator begin();

    std::set<int>::iterator end();

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);
    
    template <typename ExecutionPolicy>
    void RemoveDocument(ExecutionPolicy&& policy, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string document_content;
    };
    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> documents_id_;

    bool IsStopWord(const std::string_view& word) const;

    std::vector<std::string_view> SplitIntoWordsNoStop(const std::string_view& text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string_view& text, bool flag_sort) const;

    double ComputeWordInverseDocumentFreq(const std::string_view& word) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const std::optional<Query>& query,
                                           DocumentPredicate document_predicate) const;
    template <typename DocumentPredicate, typename ExecutionPolicy>
    std::vector<Document> FindAllDocuments(ExecutionPolicy&& policy, const std::optional<Query>& query,
                                           DocumentPredicate document_predicate) const;

    static bool IsValidWord(const std::string_view& word);
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
	: stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
	if (!all_of(stop_words.begin(), stop_words.end(), IsValidWord)) {
		throw std::invalid_argument("stop-word cannot contain characters from 0 to 31");
	}
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string_view& raw_query, DocumentPredicate document_predicate) const {
    const auto query = ParseQuery(raw_query, true);
    auto matched_documents = FindAllDocuments(query, document_predicate);
    std::sort(matched_documents.begin(), matched_documents.end(),
        [](const Document& lhs, const Document& rhs) {
            if (std::abs(lhs.relevance - rhs.relevance) < MERROR) {
                return lhs.rating > rhs.rating;
            }
            else {
                return lhs.relevance > rhs.relevance;
            }
        });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename DocumentPredicate, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view& raw_query, DocumentPredicate document_predicate) const {
	const auto query = ParseQuery(raw_query, true);
	auto matched_documents = FindAllDocuments(policy, query, document_predicate);
	std::sort(policy, matched_documents.begin(), matched_documents.end(),
		 [](const Document& lhs, const Document& rhs) {
			 if (std::abs(lhs.relevance - rhs.relevance) < MERROR) {
				 return lhs.rating > rhs.rating;
			 } else {
				 return lhs.relevance > rhs.relevance;
			 }
		 });
	if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
		matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
	}
	return matched_documents;
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view& raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, const std::string_view& raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const std::optional<Query>& query,
    DocumentPredicate document_predicate) const {
    std::map<int, double> document_to_relevance;

    for (const std::string_view& word : query->plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);

        for (const auto& [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }

    for (const std::string_view& word : query->minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto& [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    }
    std::vector<Document> matched_documents;
    for (const auto& [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }
    return matched_documents;
}

template <typename DocumentPredicate, typename ExecutionPolicy>
std::vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy&& policy, const std::optional<Query>& query,
                                       DocumentPredicate document_predicate) const {
    ConcurrentMap<int, double> document_to_relevance(THREAD_COUNT);

    std::for_each(policy, query->plus_words.cbegin(), query->plus_words.end(), [this, document_predicate, &document_to_relevance](const std::string_view& word){
        if (word_to_document_freqs_.count(word) != 0) {
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);

            for (const auto& [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
                }
            }
        }
    });
    std::for_each(policy, query->minus_words.cbegin(), query->minus_words.cend(), [this, &document_to_relevance](const std::string_view& word){
        if (word_to_document_freqs_.count(word) != 0) {
            for (const auto& [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.Erase(document_id);
            }
        }
    });
    std::vector<Document> matched_documents;
    std::map<int, double> document_to_relevance_ordinary = document_to_relevance.BuildOrdinaryMap();
    for (const auto& [document_id, relevance] : document_to_relevance_ordinary) {
        matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
    }
    return matched_documents;
}

template <typename ExecutionPolicy>
void SearchServer::RemoveDocument(ExecutionPolicy&& policy, int document_id) {
	if (documents_.count(document_id) == 0) {
		throw std::invalid_argument("document with id = " + std::to_string(document_id) + " does not exist");
	}
	documents_.erase(document_id);
	documents_id_.erase(document_id);
    
    std::vector<std::string_view> tmp(document_to_word_freqs_.at(document_id).size());
    std::transform(policy, 
                   document_to_word_freqs_.at(document_id).cbegin(),
                   document_to_word_freqs_.at(document_id).cend(),
                   tmp.begin(),
                   [](const auto& item) { return item.first; } );
    
	std::for_each(policy, tmp.begin(), tmp.end(),
              [this, document_id](const std::string_view& item) { word_to_document_freqs_[item].erase(document_id); });
    
	document_to_word_freqs_.erase(document_id);
}
