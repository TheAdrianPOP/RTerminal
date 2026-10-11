#pragma once
#ifndef RESULT_H
#define RESULT_H

#include "internal.h"
#include "move.h"

template<typename T, typename E = int>
class Result {
public:
    static Result Ok(T&& value) {
        Result res;
        new (&res.value_) T(rt_move(value));
        res.is_ok_ = true;
        return res;
    }

    static Result Ok(const T& value) {
        Result res;
        new (&res.value_) T(value);
        res.is_ok_ = true;
        return res;
    }

    static Result Error(E&& error) {
        Result res;
        new (&res.error_) E(rt_move(error));
        res.is_ok_ = false;
        return res;
    }

    static Result Error(const E& error) {
        Result res;
        new (&res.error_) E(error);
        res.is_ok_ = false;
        return res;
    }

    ~Result() {
        if (is_ok_) value_.~T();
        else        error_.~E();
    }

    Result(Result&& o) noexcept : is_ok_(o.is_ok_) {
        if (is_ok_) new (&value_) T(rt_move(o.value_));
        else        new (&error_) E(rt_move(o.error_));
    }

    Result& operator=(Result&& o) noexcept {
        if (this != &o) {
            if (is_ok_) value_.~T();
            else        error_.~E();

            is_ok_ = o.is_ok_;
            if (is_ok_) new (&value_) T(rt_move(o.value_));
            else        new (&error_) E(rt_move(o.error_));
        }
        return *this;
    }

    Result(const Result&)            = delete;
    Result& operator=(const Result&) = delete;

    bool isOk()    const { return is_ok_; }
    bool isError() const { return !is_ok_; }

    T& unwrap() {
        assert(is_ok_ && "Called unwrap() on an Error result");
        return value_;
    }

    const T& unwrap() const {
        assert(is_ok_ && "Called unwrap() on an Error result");
        return value_;
    }

    E& error() {
        assert(!is_ok_ && "Called error() on an Ok result");
        return error_;
    }

    const E& error() const {
        assert(!is_ok_ && "Called error() on an Ok result");
        return error_;
    }

private:
    Result() : is_ok_(false) {}

    union {
        T value_;
        E error_;
    };
    bool is_ok_;
};

template<typename E>
class Result<void, E> {
public:
    static Result Ok() {
        Result res;
        res.is_ok_ = true;
        return res;
    }

    static Result Error(E&& error) {
        Result res;
        new (&res.error_) E(rt_move(error));
        res.is_ok_ = false;
        return res;
    }

    static Result Error(const E& error) {
        Result res;
        new (&res.error_) E(error);
        res.is_ok_ = false;
        return res;
    }

    ~Result() {
        if (!is_ok_) error_.~E();
    }

    Result(Result&& o) noexcept : is_ok_(o.is_ok_) {
        if (!is_ok_) new (&error_) E(rt_move(o.error_));
    }

    Result& operator=(Result&& o) noexcept {
        if (this != &o) {
            if (!is_ok_) error_.~E();
            is_ok_ = o.is_ok_;
            if (!is_ok_) new (&error_) E(rt_move(o.error_));
        }
        return *this;
    }

    Result(const Result&)            = delete;
    Result& operator=(const Result&) = delete;

    bool isOk()    const { return is_ok_; }
    bool isError() const { return !is_ok_; }

    void unwrap() const {
        assert(is_ok_ && "Called unwrap() on an Error result");
    }

    E& error() {
        assert(!is_ok_ && "Called error() on an Ok result");
        return error_;
    }

    const E& error() const {
        assert(!is_ok_ && "Called error() on an Ok result");
        return error_;
    }

private:
    Result() : is_ok_(false) {}

    union {
        E error_;
    };
    bool is_ok_;
};

#endif // RESULT_H