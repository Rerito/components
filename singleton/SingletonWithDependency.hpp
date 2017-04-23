
#ifndef _SINGLETON_WITH_DEPENDENCY_HPP_INCLUDED_
#define _SINGLETON_WITH_DEPENDENCY_HPP_INCLUDED_

#include "Singleton.hpp"
#include "DependencyManager.hpp"

#include <boost/mpl/list.hpp>
#include <functional>


// This namespace holds all the helpers required to handle SingletonWitheDependency objects
namespace singleton {

// An object holding the necessary information about a registered SingletonWithDependency object
class DependencyComponent {
    bool is_deleted;
    std::size_t id;
    std::function<void()> cleaner;
public:
    DependencyComponent() : is_deleted(true), id(0u), cleaner([](){}) {}
    template <typename Ftor>
    DependencyComponent(std::size_t id, Ftor&& cleaner_ftor) : is_deleted(false), id(id), cleaner(std::forward<Ftor>(cleaner_ftor)) {}

    void clear() {
        cleaner();
        is_deleted = false;
    }
};

// This template provide the definition of the dependencies to a given singleton
// The user must specialize it in order to define the dependencies for each type.
// The default implementation provides an empty dependency list.
template <typename T>
struct registered_dependencies {
    typedef boost::mpl::list<> type;
};

// Helper typedefs for the underlying graph (Boost.Graph) and the DependencyManager based on this graph
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, DependencyComponent> DependencyGraph;
typedef DependencyManager<DependencyGraph, DependencyCleaner, std::size_t> DepManager;

// The functionalities inside this namespace are technical details
namespace details {

// This wrappers allow for a lower overhead cost when iterating over the boost::mpl::list of dependencies.
template <typename T>
struct wrapped_type {
    typedef T type;
};

// A helper class template to use in conjunction with boost::mpl::transform to wrap the dependency list inside wrapped_type
// (See the @wrapped_type_list documentation below)
template <typename T>
struct add_type_wrapper {
    typedef wrapped_type<T> type;
};

// List is a boost MPL sequence such as `boost::mpl::list<T1, ..., Tn>`
// Will define a member `type` as `boost::mpl::list<wrapped_type<T1>, ..., wrapped_type<Tn>>`
template <typename List>
struct wrapped_type_list {
    using boost::mpl::_1;
    typedef typename boost::mpl::transform<List, add_type_wrapper<_1>>::type type;
};
} // namespace details

// This class template is responsible for the registration of T's dependencies,
// T is expected to inherit from SingletonWithDependency<T>
// This functor is used along with Boost.MPL to perform the required sequence of registrations.

template <typename T>
struct DependencyRegistrar {
};

template <typename T>
struct DependencyRegistrar {
    template <typename U>
    void operator()(details::wrapped_type<U>) {
        auto& force_instanciation = SingletonWithDependency<U>::instance();
        DepManager::instance().register_dependency(typeid(U).hash_code(), typeid(T).hash_code(), boost::no_property());
    }
};

// This class template is a functor responsible for the cleaning up of a SingletonWithDependency<T>
// The default implementation does nothing, the user must define its own cleaning convention specific to each type
template <typename T>
struct Cleaner {
    void operator()(T& inst) {
        // The default implementation does nothing:
        // The user must specialize this class to include its cleaning process.
    }
};

} // namespace singleton


template <typename T>
class SingletonWithDependency {
    static bool built = false;
    static T* pInstance = nullptr;
    // The user must specialize the singleton::registered_dependencies class template
    typedef typename singleton::registered_dependencies<T>::type DependencyList;
    typedef typename singleton::details::wrapped_type_list<DependencyList>::type WrappedDependencyList;
    void do_registration() {
        using boost::mpl::_1;
        using namespace singleton;
        // Register the component with its cleaner
        DepManager::instance().register_component(typeid(T).hash_code(),
                                                  DependencyComponent(typeid(T).hash_code(), [this]() {
                                                      Cleaner<T>()(static_cast<T&>(*this));
                                                  }));
        // Register the dependencies
        boost::mpl::for_each<WrappedDependencyList>(DependencyRegistrar<T>());
    }
protected:
    SingletonWithDependency() {
        do_registration();
    }
public:
    static T& instance() {
        if (!built) {
            pInstance = new T;
        }
        return *pInstance;
    }
};



#endif // _SINGLETON_WITH_DEPENDENCY_HPP_INCLUDED_
