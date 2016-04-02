#include <unordered_map>
#include <utility>

template<typename T>
class graph {
public:
    typedef int cost;
    typedef T node;
    typedef std::unordered_map<node, cost> destination;
    typedef std::unordered_map<node, destination> _adj_list;
private:
    _adj_list adj;
public:
    graph() = default;
    inline auto add(const T& from, const T& to, cost c) {
        adj[from][to] = c;
    }

    inline auto add_bidirectional(const T& a, const T& b, cost c_ab, cost c_ba){
        add(a, b, c_ab);
        add(b, a, c_ba);
    }

    auto add_bidirectional(const T& a, const T& b, cost c){
        add_bidirectional(a, b, c, c);
    }

    const auto& adj_list() const {
        return adj;
    }
};

template<typename G, typename T>
auto starts_from(G& graph, const T& from) {
    auto& adj = graph.adj_list();
    auto dest_it = adj.find(from);
    return dest_it != adj.end() ? &dest_it->second : nullptr;
}

template<typename G, typename T>
auto edge_exists(const G& graph, const T& from, const T& to) {
    auto d = starts_from(graph, from);
        if(d) {
            return d->find(to) != d->end() ? true : false;
        }
        else return false;
}

#include <string>
#include <iostream>
template<typename T>
void print(const T* what) {
    for(auto& e : *what) {
        std::cout << e.second << std::endl;
    }
}
