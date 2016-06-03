#pragma once

#include <unordered_map>

#include "cs_cache.h"

class lru_cache : public cs_cache {
private:
    class linked_list {
    public:
        class node {
        private:
            node *_prev;
            node *_next;
            entry _value;

        public:
            node(std::shared_ptr<ndn::Data> data_ptr) :
                    _prev(nullptr), _next(nullptr), _value(data_ptr) {
            }

            ~node() {
                if (_next) {
                    delete _next;
                }
                _next = 0;
                _prev = 0;
            }

            void unlink() {
                if (_next) {
                    _next->_prev = _prev;
                }
                if (_prev) {
                    _prev->_next = _next;
                }
                _next = 0;
                _prev = 0;
            }

            node *getPrev() const {
                return _prev;
            }

            void setPrev(node *prev) {
                _prev = prev;
            }

            node *getNext() const {
                return _next;
            }

            void setNext(node *next) {
                _next = next;
            }

            entry getValue() const {
                return _value;
            }

            void setValue(entry value) {
                _value = value;
            }
        };

    private:
        node *_head;
        node *_tail;
        size_t _size;

    public:
        linked_list() :
                _head(nullptr), _tail(nullptr), _size(0) {
        }

        ~linked_list() {
            if (_head) {
                delete _head;
            }
            _head = 0;
            _tail = 0;
            _size = 0;
        }

        void push(node *node) {
            if (!_head) {
                _head = node;
            } else if (_head == _tail) {
                _head->setNext(node);
                node->setPrev(_head);
            } else {
                _tail->setNext(node);
                node->setPrev(_tail);
            }
            _tail = node;
            ++_size;
        }

        node *pop() {
            if (!_head) {
                return 0;
            } else {
                node *new_head = _head->getNext();
                _head->unlink();
                node *old_head = _head;
                _head = new_head;
                --_size;
                if (_size == 0) {
                    _tail = 0;
                }
                return old_head;
            }
        }

        node *remove(node *node) {
            if (node == _head) {
                _head = node->getNext();
            }
            if (node == _tail) {
                _tail = node->getPrev();
            }
            node->unlink();
            --_size;
            return node;
        }

        node *getHead() {
            return _head;
        }

        size_t size() {
            return _size;
        }
    };

    std::unordered_map<ndn::Name, linked_list::node> _entries;
    uint32_t _ram_size, _disk_size;
    linked_list _ram_registery, _disk_registery;

public:
    lru_cache(uint32_t ram_size, uint32_t disk_size);

    virtual ~lru_cache() override;

    virtual void insert(ndn::Data data) override;

    virtual void insert(std::shared_ptr<ndn::Data> data_ptr) override;

    virtual std::shared_ptr<ndn::Data> tryGet(ndn::Name name) override;

    virtual void checkSize();
};