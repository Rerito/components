
#ifndef _SINGLETON_HPP_INCLUDED_
#define _SINGLETON_HPP_INCLUDED_

template <typename T>
class Singleton {
    Singleton(Singleton const&) = delete;
    Singleton& operator=(Singleton const&) = delete;
protected:
    Singleton() {}
public:
    T& instance() {
        static T inst;
        return inst;
    }
};

#endif // _SINGLETON_HPP_INCLUDED_
