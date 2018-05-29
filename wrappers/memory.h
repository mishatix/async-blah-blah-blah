#ifndef MEMORY_H
#define MEMORY_H

#include <tr1/memory>

// TODO: make correct aliases of shared_ptr and weak_ptr (for C++0x standard)
using namespace std::tr1;

namespace cblp {

//template<class T> class weak_ptr;

//template<class T>
//class shared_ptr : public std::tr1::shared_ptr<T>
//{
//public:
//    shared_ptr() {}
//    shared_ptr(T* ptr) : std::tr1::shared_ptr<T>(reinterpret_cast<T*>(ptr)){}
//    shared_ptr(const std::tr1::shared_ptr<T>& r) : std::tr1::shared_ptr<T>(r) {}
//    shared_ptr(const shared_ptr<T>& r)
//        : std::tr1::shared_ptr<T>(reinterpret_cast<const std::tr1::shared_ptr<T> & >(r))
//    {}
//    shared_ptr(const std::tr1::weak_ptr<T>& r) : std::tr1::shared_ptr<T>(r.lock()) {}
//    inline shared_ptr(const weak_ptr<T>&);
//};

//template<class T>
//class weak_ptr : public std::tr1::weak_ptr<T>
//{
//public:
//    weak_ptr() {}
//    weak_ptr(const std::tr1::weak_ptr<T>& x) : std::tr1::weak_ptr<T>(x) {}
//    weak_ptr(const weak_ptr<T>& x) : std::tr1::weak_ptr<T>(reinterpret_cast<const std::tr1::weak_ptr<T> & >(x)) {}
//    weak_ptr (const std::tr1::shared_ptr<T>& x) : std::tr1::weak_ptr<T>(x) {}
//    weak_ptr (const shared_ptr<T>& x) : std::tr1::weak_ptr<T>(reinterpret_cast<const std::tr1::shared_ptr<T> & >(x)) {}
//};

//template<class T>
//inline shared_ptr<T>::shared_ptr(const weak_ptr<T>& r) : std::tr1::shared_ptr<T>(r.lock()) {}

// TODO:
// add memory pools implementation

} // namespace cblp


#endif // MEMORY_H
