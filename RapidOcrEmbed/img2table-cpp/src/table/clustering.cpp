#include "table/clustering.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace table {

std::vector<std::vector<int>> findComponents(const std::vector<std::set<int>>& edges) {
    std::map<int, std::set<int>> adj;
    for (auto& e : edges) {
        std::vector<int> v(e.begin(), e.end());
        for (auto node : v) adj[node].insert(node);
        if (v.size() == 2) {
            adj[v[0]].insert(v[1]);
            adj[v[1]].insert(v[0]);
        }
    }

    std::set<int> visited;
    std::vector<std::vector<int>> components;

    for (auto& [node, _] : adj) {
        if (visited.count(node)) continue;
        std::vector<int> stack = {node};
        visited.insert(node);
        std::vector<int> comp;
        while (!stack.empty()) {
            int current = stack.back();
            stack.pop_back();
            comp.push_back(current);
            for (int neighbor : adj[current]) {
                if (!visited.count(neighbor)) {
                    visited.insert(neighbor);
                    stack.push_back(neighbor);
                }
            }
        }
        components.push_back(comp);
    }
    return components;
}

std::vector<std::vector<Cell>> clusterCellsInTables(const std::vector<Cell>& cells) {
    if (cells.empty()) return {};

    int n = static_cast<int>(cells.size());
    std::vector<std::set<int>> edges;

    for (int i = 0; i < n; i++) {
        edges.push_back({i});
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double x1i = cells[i].x1, y1i = cells[i].y1;
            double x2i = cells[i].x2, y2i = cells[i].y2;
            double x1j = cells[j].x1, y1j = cells[j].y1;
            double x2j = cells[j].x2, y2j = cells[j].y2;
            double w_i = x2i - x1i, h_i = y2i - y1i;
            double w_j = x2j - x1j, h_j = y2j - y1j;

            double xOverlap = std::min(x2i, x2j) - std::max(x1i, x1j);
            double yOverlap = std::min(y2i, y2j) - std::max(y1i, y1j);

            double diffX = std::min({
                std::abs(x1i - x1j), std::abs(x1i - x2j),
                std::abs(x2i - x1j), std::abs(x2i - x2j)
            });
            double diffY = std::min({
                std::abs(y1i - y1j), std::abs(y1i - y2j),
                std::abs(y2i - y1j), std::abs(y2i - y2j)
            });

            double threshX = std::min(5.0, 0.05 * std::min(w_i, w_j));
            double threshY = std::min(5.0, 0.05 * std::min(h_i, h_j));

            bool adjacent = ((yOverlap > 5) && (diffX <= threshX)) ||
                           ((xOverlap > 5) && (diffY <= threshY));

            if (adjacent) {
                edges.push_back({i, j});
            }
        }
    }

    auto components = findComponents(edges);
    std::vector<std::vector<Cell>> result;
    for (auto& comp : components) {
        std::vector<Cell> cluster;
        for (int idx : comp) cluster.push_back(cells[idx]);
        result.push_back(cluster);
    }
    return result;
}

} // namespace table
