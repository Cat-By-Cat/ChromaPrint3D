#include "region_merge.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ChromaPrint3D::detail {

namespace {

struct Region {
    int id;
    double sum_l = 0, sum_a = 0, sum_b = 0;
    int area = 0;
    std::unordered_set<int> neighbors;

    double AvgL() const { return sum_l / area; }

    double AvgA() const { return sum_a / area; }

    double AvgB() const { return sum_b / area; }

    double ColorDistSq(const Region& o) const {
        double dl = AvgL() - o.AvgL();
        double da = AvgA() - o.AvgA();
        double db = AvgB() - o.AvgB();
        return dl * dl + da * da + db * db;
    }
};

struct MergePair {
    int r1, r2;
    double cost;

    bool operator>(const MergePair& o) const { return cost > o.cost; }
};

double MergeCost(const Region& a, const Region& b) { return a.ColorDistSq(b); }

int Find(std::vector<int>& parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x         = parent[x];
    }
    return x;
}

void Unite(std::vector<int>& parent, std::vector<int>& rank, int a, int b) {
    a = Find(parent, a);
    b = Find(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) std::swap(a, b);
    parent[b] = a;
    if (rank[a] == rank[b]) ++rank[a];
}

} // namespace

int MergeRegions(cv::Mat& labels, const cv::Mat& image, double lambda, int min_area) {
    const int rows = labels.rows;
    const int cols = labels.cols;

    // Collect regions: accumulate color sums and areas
    std::unordered_map<int, Region> regions;
    for (int r = 0; r < rows; ++r) {
        const int* lrow       = labels.ptr<int>(r);
        const cv::Vec3f* irow = image.ptr<cv::Vec3f>(r);
        for (int c = 0; c < cols; ++c) {
            int lid   = lrow[c];
            auto& reg = regions[lid];
            reg.id    = lid;
            reg.sum_l += irow[c][0];
            reg.sum_a += irow[c][1];
            reg.sum_b += irow[c][2];
            reg.area++;
        }
    }

    // Build adjacency by scanning 4-connected neighbors
    for (int r = 0; r < rows; ++r) {
        const int* lrow      = labels.ptr<int>(r);
        const int* lrow_next = (r + 1 < rows) ? labels.ptr<int>(r + 1) : nullptr;
        for (int c = 0; c < cols; ++c) {
            int lid = lrow[c];
            if (c + 1 < cols && lrow[c + 1] != lid) {
                int nid = lrow[c + 1];
                regions[lid].neighbors.insert(nid);
                regions[nid].neighbors.insert(lid);
            }
            if (lrow_next && lrow_next[c] != lid) {
                int nid = lrow_next[c];
                regions[lid].neighbors.insert(nid);
                regions[nid].neighbors.insert(lid);
            }
        }
    }

    // Remap region ids to contiguous range for union-find
    std::vector<int> id_list;
    id_list.reserve(regions.size());
    for (auto& [k, _] : regions) id_list.push_back(k);
    std::sort(id_list.begin(), id_list.end());

    int max_id = id_list.empty() ? 0 : id_list.back() + 1;
    std::vector<int> parent(max_id);
    std::vector<int> uf_rank(max_id, 0);
    std::iota(parent.begin(), parent.end(), 0);

    // Priority queue — min-cost first
    std::priority_queue<MergePair, std::vector<MergePair>, std::greater<>> pq;

    auto push_pair = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        double c = MergeCost(regions[a], regions[b]);
        pq.push({a, b, c});
    };

    for (auto& [rid, reg] : regions) {
        for (int nid : reg.neighbors) {
            if (nid > rid) push_pair(rid, nid);
        }
    }

    // Greedy merge
    while (!pq.empty()) {
        auto [r1, r2, cost] = pq.top();
        pq.pop();

        int a = Find(parent, r1);
        int b = Find(parent, r2);
        if (a == b) continue;

        auto it_a = regions.find(a);
        auto it_b = regions.find(b);
        if (it_a == regions.end() || it_b == regions.end()) continue;

        Region& ra = it_a->second;
        Region& rb = it_b->second;

        if (!ra.neighbors.count(b)) continue;

        double actual_cost = MergeCost(ra, rb);

        // Area criterion or force-merge tiny regions
        bool force = (ra.area < min_area || rb.area < min_area);
        if (!force && actual_cost >= lambda) continue;

        // Merge b into a
        Unite(parent, uf_rank, a, b);
        int root = Find(parent, a);

        Region merged;
        merged.id    = root;
        merged.sum_l = ra.sum_l + rb.sum_l;
        merged.sum_a = ra.sum_a + rb.sum_a;
        merged.sum_b = ra.sum_b + rb.sum_b;
        merged.area  = ra.area + rb.area;

        // Merge neighbor sets
        merged.neighbors = std::move(ra.neighbors);
        for (int n : rb.neighbors) merged.neighbors.insert(n);
        merged.neighbors.erase(a);
        merged.neighbors.erase(b);

        // Update neighbors' adjacency
        for (int n : merged.neighbors) {
            int nr  = Find(parent, n);
            auto it = regions.find(nr);
            if (it == regions.end()) continue;
            it->second.neighbors.erase(a);
            it->second.neighbors.erase(b);
            it->second.neighbors.insert(root);
        }

        if (root != a) regions.erase(a);
        if (root != b) regions.erase(b);
        regions[root] = std::move(merged);

        // Re-enqueue pairs with new neighbors
        for (int n : regions[root].neighbors) {
            int nr = Find(parent, n);
            if (nr != root) push_pair(root, nr);
        }
    }

    // Relabel the label map with contiguous ids
    std::unordered_map<int, int> remap;
    int next_id = 0;
    for (int r = 0; r < rows; ++r) {
        int* lrow = labels.ptr<int>(r);
        for (int c = 0; c < cols; ++c) {
            int root = Find(parent, lrow[c]);
            auto it  = remap.find(root);
            if (it == remap.end()) {
                remap[root] = next_id;
                lrow[c]     = next_id++;
            } else {
                lrow[c] = it->second;
            }
        }
    }

    return next_id;
}

} // namespace ChromaPrint3D::detail
