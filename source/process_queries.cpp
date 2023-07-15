#include <algorithm>
#include <execution>

#include "process_queries.h"

std::vector<std::vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> res(queries.size());
    std::transform(std::execution::par,
                  queries.begin(),
                  queries.end(),
                  res.begin(),
                  [&search_server](const std::string& s) { return search_server.FindTopDocuments(s); });
    
    return res;
}

std::deque<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> res(queries.size());
    std::transform(std::execution::par,
                  queries.begin(),
                  queries.end(),
                  res.begin(),
                  [&search_server](const std::string& s) { return search_server.FindTopDocuments(s); });
    std::deque<Document> dq;
    for (const auto& docs : res) {
        dq.insert(dq.end(), docs.begin(), docs.end());
    }    
    return dq;
}