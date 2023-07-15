#pragma once

#include <iostream>
#include <iterator>
#include <vector>

template <typename Iterator>
class IteratorRange {
public:
	IteratorRange(const Iterator it_begin, const Iterator it_end) : page_(it_begin, it_end) {}

	Iterator begin() const {
		return page_.first;
	}

	Iterator end() const {
		return page_.second;
	}

	size_t size() const {
		return distance(page_.first, page_.second);
	}
private:
	const std::pair<Iterator, Iterator> page_;
};

template <typename Iterator>
std::ostream& operator<<(std::ostream& out, const IteratorRange<Iterator> range) {
	for (auto i = range.begin(); i != range.end(); ++i) {
		out << *i;
	}
	return out;
}

template <typename Iterator>
class Paginator {
public:
        Paginator(const Iterator it_begin, const Iterator it_end, int page_size) {
        	for (auto page_begin = it_begin; page_begin < it_end; ) {
        		auto page_end = page_begin;
        		if (distance(page_begin, it_end) >= page_size) {
        			std::advance(page_end, page_size);
        		} else {
        			page_end = it_end;
        		}
				auto page = IteratorRange<Iterator>(page_begin, page_end);
				pages_.push_back(page);
				page_begin = page_end;
        	}
        }

        auto begin() const {
        	return pages_.begin();
        }

        auto end() const {
			return pages_.end();
		}

        size_t size() const {
        	return pages_.size();
        }

private:
	std::vector<IteratorRange<Iterator>> pages_;
};