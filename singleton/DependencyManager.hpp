
#ifndef _DEPENDENCY_MANAGER_HPP_INCLUDED_
#define _DEPENDENCY_MANAGER_HPP_INCLUDED_

#include "Singleton.h"

#include <string>
#include <stdexcept> // For runtime_error
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/filtered_graph.hpp>

#include <boost/graph/breadth_first_search.hpp>

namespace graph_details {

struct breaking_condition_exception : public std::exception {}; 

struct no_halting {
    template <typename GraphElement, typename Graph>
    bool operator()(GraphElement const&, Graph const&) {
        return false;
    }
};

// Just to avoid most vexing parse :)
no_halting make_no_halting() {
    return no_halting();
}

// A proxy BFS visitor for the BGL
// This proxy allows to terminate the BFS when a given predicate is met by throwing an exception
// (This is the only sane way to interrupt a BGL BFS)
template <typename VertexPredicate = no_halting, typename EdgePredicate = no_halting, typename BFSVisitor = boost::default_bfs_visitor>
class bfs_halting_visitor : public BFSVisitor {
public:
    // VisArgs would be a variadic pack after a migration to VS2013 and onwards
    template <typename VPred, typename EPred, typename VisArgs>
    bfs_halting_visitor(VPred&& v_pred, EPred&& e_pred, VisArgs&& args) : BFSVisitor(std::forward<VisArgs>(args)), vpred(std::forward<VPred>(v_pred)), epred(std::forward<EPred>(e_pred)) {}
    
    // Check the Edge Predicate
    // Forward the examine_edge call to the base visitor and throw if the predicate is met
    template <typename Edge, typename Graph>
    void examine_edge(Edge&& e, Graph&& g) {
        bool result = epred(e, g);
        BFSVisitor::examine_edge(std::forward<Edge>(e), std::forward<Graph>(g));
        if (result) {
            throw breaking_condition_exception;
        }
    }
   
    // Check the Vertex Predicate
    // Forward the discover_vertex call to the base visitor and throw if the predicate is met
    template <typename Vertex, typename Graph>
    void discover_vertex(Vertex&& v, Graph&& g) {
        bool result = vpred(v, g);
        BFSVisitor::discover_vertex(std::forward<Vertex>(v), std::forward<Graph>(g));
        if (result) {
            throw breaking_condition_exception;
        }
    }
    
private:
    VertexPredicate vpred;
    EdgePredicate epred;
}; // bfs_halting_visitor

} // namespace graph_details

template <typename DepGraph, typename Deleter, typename ID = std::string>
class DependencyManager : public Singleton<DependencyManager> {
    typedef boost::graph_traits<DepGraph> GraphTraits;
    typedef typename GraphTraits::vertex_descriptor Vertex;
    typedef typename GraphTraits::edge_descriptor Edge;
    typedef typename boost::vertex_bundle_type<DepGraph>::type VertexProperty;
    typedef typename boost::edge_bundle_type<DepGraph>::type EdgeProperty;

    // Add static_assert to explicit requirements on Vertex/Edge properties
    DepGraph m_dependency_graph;
    std::unordered_map<ID, Vertex> m_vertices_map;

    // A helper class to establish the deletion stack (to perform deletion of components in an orderly fashion)
    class DeletionStackBfsVisitor : public boost::default_bfs_visitor {
        std::vector<Vertex>& m_deletion_stack;
    public:
        DeletionStackBfsVisitor(std::vector<Vertex>& del_stack) : m_deletion_stack(del_stack) {}
        
        // We template on graph to allow the visitor to process filtered graph from our dependency graph
        template <typename Graph>
        void discover_vertex(Vertex const& v, Graph const&) {
            m_deletion_stack.push_back(v);
        }
    }; // DeletionStackBfsVisitor
    

    //***************************************************************//
    //****************** CLEAN UP MEMBER FUNCTIONS ******************//
    //***************************************************************//
    
    // The VertexPredicate aims at detecting which Vertices have already been deleted
    // This returns a vector containing the elements that needs to be deleted
    // This is a LIFO output: the elements must be deleted in reverse order
    template <typename VertexPredicate>
    std::vector<Vertex> get_deletion_stack(Vertex const& root, VertexPredicate&& pred) const {
        typedef boost::filtered_graph<DepGraph, boost::keep_all, VertexPredicate> FilteredGraph;
        std::vector<Vertex> result_stack;
        boost::breadth_first_search(FilteredGraph(m_dependency_graph, boost::keep_all(), std::forward<VertexPredicate>(pred)),
                                    root,
                                    boost::visitor(DeletionStackBfsVisitor(result_stack)));
        return result_stack;
    }

    bool is_root_dependency(Vertex const& v) const {
        auto const in_es = in_edges(v, m_dependency_graph);
        return in_es.first == in_es.second;
    }

    void perform_deletion_from(Vertex const& v) {
        Deleter del;
        // Replace that keep_all with an actual predicate
        auto del_stack = get_deletion_stack(v, boost::keep_all());
        for (auto v_rit = del_stack.rbegin(); del_stack.rend() != v_rit; ++v_rit) {
            del(m_dependency_graph[*v_rit]);
        }
    }

    void clear() {
        auto const vs = vertices(m_dependency_graph);
        for (auto v_it = vs.first; vs.second != v_it; ++v_it) {
            if (is_root_dependency(*v_it)) {
                perform_deletion_from(*v_it);
            }
        }
    }

    //***************************************************************//
    //***************************************************************//

    void detect_cycle() const {
        // Would the dependency introduce a cycle in the graph?
        typedef graph_details::bfs_halting_visitor<std::function<bool(Vertex const&, DepGraph const&)>> CycleDetectionVisitor;
        try {
            // To do so we conduct a Breadth-First Search on the graph, starting from the target of the dependency.
            // If the BFS reaches the source of the dependency, it means the source already depends on the target which is not allowed.
            boost::default_bfs_visitor base_vis;
            CycleDetectionVisitor vis([&](Vertex const& v, DepGraph const&) {
                                          return (v == src_it->second);
                                      },
                                      graph_details::make_no_halting(),
                                      std::move(base_vis));
            boost::breadth_first_search(m_dependency_graph, dst_it->second, boost::visitor(vis));
        } catch (graph_details::breaking_condition_exception& e) {
            // If it would, then we must abort the process and throw
            throw std::runtime_error("Registering the dependency would produce a cycle in the dependency graph.");
        }
    }


public:
    // Templated to allow perfect forwarding in case VertexProperty isn't copyable
    // Ideally it would be a variadic pack to allow access to any VertexProperty constructor.
    template <typename VertexProp>
    void register_component(ID const& id, VertexProp&& properties) {
        if (m_vertices_map.find(id) == m_vertices_map.end()) {
            auto v = add_vertex(VertexProperty(std::forward<VertexProp>(properties)), m_dependency_graph);
            m_vertices_map.emplace(std::make_pair(id, v));
        } else {
            throw std::runtime_error("Object is already registered");
        }
    }

    // Same as for @register_component
    // Ideally we would use a variadic pack to carry the parameters required to create the edge property object
    // This registers a dependency between @src_id and @dst_id where @dst_id depends on @src_id
    // @param src_id The component being depended on
    // @param dst_id The component that depends on @src_id
    // @param edge_prop The edge property object to attach to the dependency relationship
    template <typename EdgeProp>
    void register_dependency(ID const& src_id, ID const& dst_id, EdgeProp&& edge_prop) {
        // Here, the source of the dependency is the object on which the target depends i.e. target depends on source.
        // We begin by checking that the input IDs are valid and that the dependency relationship isn't registered yet.
        auto src_it = m_vertices_map.find(src_id), dst_id = m_vertices_map.find(dst_id);
        if (m_vertices_map.end() == src_it || m_vertices_map.end() == dst_it) {
            throw std::runtime_error("The source and/or the target of the dependency relationship is not registered in the graph.");
        } else if (edge(src_it->second, dst_it->second, m_dependency_graph).second) {
            throw std::runtime_error("The described dependency is already registered.");
        }

        // If adding the dependency would introduce a cycle, the following call will throw
        detect_cycle();
        // Otherwise, we carry on. We add the directed edge meaning "this depends on me"
        auto added_edge = add_edge(src_it->second, dst_it->second, m_dependency_graph);
        m_dependency_graph[added_edge.first] = EdgeProperty(std::forward<EdgeProp>(edge_prop));
    }
}; // DependencyManager


#endif // _DEPENDENCY_MANAGER_HPP_INCLUDED_

