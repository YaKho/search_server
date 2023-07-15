#pragma once

#include <deque>

#include "search_server.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);

    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string_view& raw_query, DocumentPredicate document_predicate);
    
    std::vector<Document> AddFindRequest(const std::string_view& raw_query, DocumentStatus status);
    
    std::vector<Document> AddFindRequest(const std::string_view& raw_query);
    
    int GetNoResultRequests() const;
private:
    struct QueryResult {
    	std::vector<Document> documents;
    };
    std::deque<QueryResult> requests_;
    const static int min_in_day_ = 1440;
    const SearchServer& search_server_;
    int empty_request_ = 0;
    int current_time_ = 0;
};

template <typename DocumentPredicate>
std::vector<Document> RequestQueue::AddFindRequest(const std::string_view& raw_query, DocumentPredicate document_predicate) {
	QueryResult result;
	result.documents = search_server_.FindTopDocuments(raw_query, document_predicate);
	current_time_++;
	requests_.push_back(result);
	if (result.documents.empty()) {
		empty_request_++;
	}
	if (current_time_ > min_in_day_) {
		QueryResult request = requests_.front();
		requests_.pop_front();
		if (request.documents.empty()) {
			empty_request_--;
		}
		current_time_--;
	}
	return result.documents;
}
