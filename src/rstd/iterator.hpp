#pragma once

#include <type_traits>
#include "prelude.hpp"


namespace rstd {

template <template <typename...> typename Cont>
struct FromIterator {
    template <typename T, typename I> 
    static Cont<T> from_iter(I &&iter) {
        Cont<T> cont;
        for (;;) {
            auto ne = iter.next();
            if (ne.is_some()) {
                cont.push_back(std::move(ne.unwrap()));
            } else {
                break;
            }
        }
        return cont;
    }
};

namespace iter {

template <typename T, typename I, typename F>
class Map;
template <typename T, typename I, typename F>
class Filter;
template <typename T, typename I, typename F>
class FilterMap;
template <typename T, typename I>
class Cycle;
template <typename T, typename I, typename J>
class Chain;

} // namespace iter

template <typename T, typename Self>
class Iterator {
private:
    //static_assert(std::is_base_of_v<Iterator<Self, T>, Self>);
    Self &self() { return *static_cast<Self *>(this); }
    const Self &self() const { return *static_cast<const Self *>(this); }

public:
    typedef T Item;

    template <typename F>
    rstd::Option<T> find(F &&f) {
        static_assert(std::is_same_v<std::invoke_result_t<F, T>, bool>);
        for (;;) {
            auto res = self().next();
            if (res.is_some()) {
                T x = res.unwrap();
                if (f(x)) {
                    return rstd::Some(std::move(x));
                }
            } else {
                return rstd::None();
            }
        }
    }
    template <typename F, typename U=option_some_type<std::invoke_result_t<F, T>>>
    rstd::Option<T> find_map(F &&f) {
        static_assert(std::is_same_v<std::invoke_result_t<F, T>, Option<U>>);
        for (;;) {
            auto res = self().next();
            if (res.is_some()) {
                Option<U> ox = f(res.unwrap());
                if (ox.is_some()) {
                    return ox;
                }
            } else {
                return rstd::None();
            }
        }
    }
    template <template <typename...> typename C>
    C<T> collect() {
        return FromIterator<C>::template from_iter<T>(std::move(self()));
    }
    
    template <typename F>
    iter::Map<T, Self, F> map(F &&f) {
        return iter::Map<T, Self, F>(std::move(self()), std::move(f));
    }
    template <typename F>
    iter::Filter<T, Self, F> filter(F &&f) {
        return iter::Filter<T, Self, F>(std::move(self()), std::move(f));
    }
    template <typename F>
    iter::FilterMap<T, Self, F> filter_map(F &&f) {
        return iter::FilterMap<T, Self, F>(std::move(self()), std::move(f));
    }
    iter::Cycle<T, Self> cycle() {
        return iter::Cycle<T, Self>(std::move(self()));
    }
    template <typename J>
    iter::Chain<T, Self, J> chain(J &&other) {
        return iter::Chain<T, Self, J>(std::move(self()), std::move(other));
    }

    template <typename T_=T, typename X=std::enable_if_t<std::is_pointer_v<T_>, void>>
    decltype(auto) copied() {
        return self().map([](T_ x) { return *x; });
    }

private:
    class End {};
    class Cur {
    private:
        Self *owner;
        Option<T> value;
    public:
        Cur(Self *o) : owner(o), value(o->next()) {}
        bool operator!=(End) const {
            return value.is_some();
        }
        Cur &operator++() {
            value = owner->next();
            return *this;
        }
        T &operator*() {
            return value.get();
        }
    };
    
public:
    Cur begin() {
        return Cur(&self());
    }
    End end() {
        return End();
    }
};

namespace iter {

template <typename T, typename I, typename F>
class Map final : public Iterator<T, Map<T, I, F>> {
private:
    I iter;
    F func;
public:
    Map(I &&i, F &&f) :
        iter(std::move(i)), func(std::move(f))
    {}
    rstd::Option<std::invoke_result_t<F, T>> next() {
        return iter.next().map(func);
    }
    typedef Map<T, typename I::Rev, F> Rev;
    Rev rev() {
        return Rev(iter.rev(), std::move(func));
    }
};
template <typename T, typename I, typename F>
class Filter final : public Iterator<T, Filter<T, I, F>> {
private:
    I iter;
    F func;
public:
    Filter(I &&i, F &&f) :
        iter(std::move(i)), func(std::move(f))
    {}
    rstd::Option<T> next() {
        return iter.find(func);
    }
    typedef Filter<T, typename I::Rev, F> Rev;
    Rev rev() {
        return Rev(iter.rev(), std::move(func));
    }
};
template <typename T, typename I, typename F>
class FilterMap final : public Iterator<T, FilterMap<T, I, F>> {
private:
    I iter;
    F func;
public:
    FilterMap(I &&i, F &&f) :
        iter(std::move(i)), func(std::move(f))
    {}
    rstd::Option<option_some_type<std::invoke_result_t<F, T>>> next() {
        return iter.find_map(func);
    }
    typedef FilterMap<T, typename I::Rev, F> Rev;
    Rev rev() {
        return Rev(iter.rev(), std::move(func));
    }
};
template <typename T, typename I>
class Cycle final : public Iterator<T, Cycle<T, I>> {
private:
    I origin, iter;
public:
    Cycle(I &&i) :
        origin(clone(i)),
        iter(std::move(i))
    {}
    rstd::Option<T> next() {
        auto ox = iter.next();
        if (ox.is_some()) {
            return ox;
        } else {
            iter = clone(origin);
            return iter.next();
        }
    }
};
template <typename T, typename I, typename J>
class Chain final : public Iterator<T, Chain<T, I, J>> {
private:
    I first;
    J second;
public:
    Chain(I &&i, J &&j) :
        first(std::move(i)),
        second(std::move(j))
    {}
    rstd::Option<T> next() {
        return first.next().or_else([&]() { return second.next(); });
    }
    typedef Chain<T, typename J::Rev, typename I::Rev> Rev;
    Rev rev() {
        return Rev(second.rev(), first.rev());
    }
};

} // namespace iter

template <template <typename...> typename C, typename T, typename U, typename J>
class Iter : public Iterator<U, Iter<C, T, U, J>> {
private:
    J cur, end;
public:
    Iter(J begin, J end) :
        cur(begin), end(end)
    {}
    Iter(const Iter &) = default;
    Iter &operator=(const Iter &) = default;
    Iter(Iter &&other) :
        cur(std::move(other.cur)),
        end(std::move(other.end))
    {
        other.cur = other.end;
    }
    Iter &operator=(Iter &&other) {
        cur = std::move(other.cur);
        end = std::move(other.end);
        other.cur = other.end;
        return *this;
    }
    rstd::Option<U> next() {
        if (cur != end) {
            U i = &*cur;
            ++cur;
            return Option<U>::Some(i);
        } else {
            return Option<U>::None();
        }
    }
    typedef Iter<C, T, U, std::reverse_iterator<J>> Rev;
    Rev rev() {
        auto r = Rev(
            std::reverse_iterator<J>(end),
            std::reverse_iterator<J>(cur)
        );
        cur = end;
        return r;
    }
};

template <
    template <typename...> typename C,
    typename T,
    typename I=Iter<C, T, const T *, typename C<T>::const_iterator>
>
I iter_ref(const C<T> &cont) {
    return I(cont.begin(), cont.end());
}
template <
    template <typename...> typename C,
    typename T,
    typename I=Iter<C, T, T *, typename C<T>::iterator>
>
I iter_ref(C<T> &cont) {
    return I(cont.begin(), cont.end());
}

template <template <typename...> typename C, typename T, typename J>
class IntoIter : public Iterator<T, IntoIter<C, T, J>> {
private:
    C<T> cont;
    J cur, end;
public:
    IntoIter(C<T> &&c, J begin, J end) :
        cont(std::move(c)),
        cur(begin), end(end)
    {}
    IntoIter(const IntoIter &) = default;
    IntoIter &operator=(const IntoIter &) = default;
    IntoIter(IntoIter &&other) :
        cont(std::move(other.cont)),
        cur(std::move(other.cur)),
        end(std::move(other.end))
    {
        other.cur = other.end;
    }
    IntoIter &operator=(IntoIter &&other) {
        cont = std::move(other.cont);
        cur = std::move(other.cur);
        end = std::move(other.end);
        other.cur = other.end;
        return *this;
    }
    rstd::Option<T> next() {
        if (cur != end) {
            T t = std::move(*cur);
            ++cur;
            return Option<T>::Some(std::move(t));
        } else {
            return Option<T>::None();
        }
    }
    typedef IntoIter<C, T, std::reverse_iterator<J>> Rev;
    Rev rev() {
        auto r = Rev(
            std::move(cont),
            std::reverse_iterator<J>(end),
            std::reverse_iterator<J>(cur)
        );
        cur = end;
        return r;
    }
};
template <
    template <typename...> typename C,
    typename T,
    typename I=IntoIter<C, T, typename C<T>::iterator>
>
I into_iter(C<T> &&cont) {
    return I(std::move(cont), cont.begin(), cont.end());
}

template <typename T>
struct Range : public Iterator<T, Range<T>> {
private:
    T start_ = 0, end_ = 0;
    bool rev_ = false;

public:
    explicit Range(T e) : Range(0, e) {}
    Range(T s, T e) : start_(s), end_(e) {}
    Range(const Range &) = default;
    Range &operator=(const Range &) = default;
    Range(Range &&other) :
        start_(other.start_),
        end_(other.end_),
        rev_(other.rev_)
    {
        other.start_ = other.end_;
    }
    Range &operator=(Range &&other) {
        start_ = other.start_;
        end_ = other.end_;
        rev_ = other.rev_;
        other.start_ = other.end_;
        return *this;
    }
    rstd::Option<T> next() {
        if (start_ < end_) {
            T i;
            if (!rev_) {
                i = start_;
                ++start_;
            } else {
                --end_;
                i = end_;
            }
            return Option<T>::Some(i);
        } else {
            return Option<T>::None();
        }
    }
    typedef Range Rev;
    Range rev() {
        Range r = clone(*this);
        r.rev_ = !rev_;
        start_ = end_;
        return r;
    }
};

} // namespace rstd
