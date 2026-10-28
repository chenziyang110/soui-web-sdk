#ifndef TaskSchedulingIndex_h
#define TaskSchedulingIndex_h
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
namespace content { namespace task_scheduling {

// Owning containers retain payload lifetime. This is an intrusive indexed view:
// one pointer per active item. Four canonical empty buckets retain capacity;
// other empty nodes retire. Cached statistics include retained vector capacity.
class DeadlinePriorityIndex {
public:
    struct Entry {
        void* value = nullptr;
        double deadline = 0;
        int priority = 0;
        std::size_t position = absent();
        std::uint64_t order = 0;
        static std::size_t absent() { return (std::numeric_limits<std::size_t>::max)(); }
    };
    struct Statistics { std::size_t entries = 0, nodes = 0, bytes = 0; };
    Statistics statistics() const noexcept { return m_root ? m_root->statistics : Statistics{}; }
    void prepare(int priority, bool replacing = false)
    {
        if (m_order == (std::numeric_limits<std::uint64_t>::max)())
            throw std::overflow_error("scheduler index order exhausted");
        Node* node = find(m_root.get(), priority);
        if (!node) {
            auto created = std::unique_ptr<Node>(new Node(priority));
            created->items.reserve(4);
            m_root = ensure(std::move(m_root), std::move(created));
            node = find(m_root.get(), priority);
        }
        if (!replacing && node->items.size() == node->items.capacity()) {
            const std::size_t size = node->items.size();
            if (size == node->items.max_size()) throw std::bad_alloc();
            const std::size_t grown = size > node->items.max_size() / 2 ? node->items.max_size() : (size ? size * 2 : 4);
            node->items.reserve(grown);
        }
        refresh(m_root.get(), priority);
    }
    void discardEmpty(int priority) noexcept
    {
        const Node* node = find(m_root.get(), priority);
        if (node && node->items.empty() && !retained(priority)) m_root = remove(std::move(m_root), priority);
    }
    void insert(Entry& entry) noexcept
    {
        Node* node = find(m_root.get(), entry.priority);
        entry.order = ++m_order;
        entry.position = node->items.size();
        node->items.push_back(&entry); // prepare() reserves before caller mutation.
        up(node->items, entry.position);
        refresh(m_root.get(), entry.priority);
    }
    void erase(Entry& entry, bool keepEmpty = false) noexcept
    {
        if (entry.position == Entry::absent()) return;
        Node* node = find(m_root.get(), entry.priority);
        auto& items = node->items;
        const std::size_t position = entry.position;
        if (position + 1 != items.size()) { items[position] = items.back(); items[position]->position = position; }
        items.pop_back(); entry.position = Entry::absent();
        if (position < items.size()) {
            if (position && earlier(items[position], items[(position - 1) / 2])) up(items, position);
            else down(items, position);
        }
        if (items.empty() && !keepEmpty && !retained(entry.priority)) m_root = remove(std::move(m_root), entry.priority);
        else refresh(m_root.get(), entry.priority);
    }
    Entry* firstEligible(double now) const noexcept { return eligible(m_root.get(), now); }
    Entry* oldest() const noexcept
    {
        return m_root && m_root->minimum != infinity() ? eligible(m_root.get(), m_root->minimum) : nullptr;
    }
    Entry* select(double now, bool draining) const noexcept
    {
        Entry* first = oldest();
        if (!first) return nullptr;
        if (!draining && first->deadline <= now - 0.050) return first;
        return firstEligible(draining ? infinity() : now);
    }
    bool hasReadyAtOrAbove(int priority, double now) const noexcept
    {
        const Entry* entry = firstEligible(now);
        return entry && entry->priority >= priority;
    }
private:
    struct Node {
        explicit Node(int value) : priority(value) {}
        int priority, height = 1;
        double minimum = infinity();
        std::vector<Entry*> items;
        std::unique_ptr<Node> left, right;
        Statistics statistics;
    };
    static bool retained(int priority) noexcept { return priority == 0 || priority == 100 || priority == 200 || priority == 300; }
    static double infinity() { return (std::numeric_limits<double>::infinity)(); }
    static int height(const std::unique_ptr<Node>& node) { return node ? node->height : 0; }
    static void update(Node* node) noexcept
    {
        node->height = 1 + (std::max)(height(node->left), height(node->right));
        node->minimum = node->items.empty() ? infinity() : node->items.front()->deadline;
        if (node->left) node->minimum = (std::min)(node->minimum, node->left->minimum);
        if (node->right) node->minimum = (std::min)(node->minimum, node->right->minimum);
        node->statistics = {node->items.size(), 1, sizeof(Node) + node->items.capacity() * sizeof(Entry*)};
        for (const Node* child : {node->left.get(), node->right.get()}) if (child) {
            node->statistics.entries += child->statistics.entries;
            node->statistics.nodes += child->statistics.nodes;
            node->statistics.bytes += child->statistics.bytes;
        }
    }
    static std::unique_ptr<Node> rotateLeft(std::unique_ptr<Node> node) noexcept
    {
        auto next = std::move(node->right);
        node->right = std::move(next->left); update(node.get());
        next->left = std::move(node); update(next.get()); return next;
    }
    static std::unique_ptr<Node> rotateRight(std::unique_ptr<Node> node) noexcept
    {
        auto next = std::move(node->left);
        node->left = std::move(next->right); update(node.get());
        next->right = std::move(node); update(next.get()); return next;
    }
    static std::unique_ptr<Node> balance(std::unique_ptr<Node> node) noexcept
    {
        if (!node) return node;
        update(node.get());
        const int difference = height(node->left) - height(node->right);
        if (difference > 1) {
            if (height(node->left->left) < height(node->left->right)) node->left = rotateLeft(std::move(node->left));
            return rotateRight(std::move(node));
        }
        if (difference < -1) {
            if (height(node->right->right) < height(node->right->left)) node->right = rotateRight(std::move(node->right));
            return rotateLeft(std::move(node));
        }
        return node;
    }
    static std::unique_ptr<Node> ensure(std::unique_ptr<Node> node, std::unique_ptr<Node> created) noexcept
    {
        if (!node) return created;
        if (created->priority < node->priority) node->left = ensure(std::move(node->left), std::move(created));
        else node->right = ensure(std::move(node->right), std::move(created));
        return balance(std::move(node));
    }
    static Node* find(Node* node, int priority) noexcept
    {
        while (node && node->priority != priority) node = priority < node->priority ? node->left.get() : node->right.get();
        return node;
    }
    static void refresh(Node* node, int priority) noexcept
    {
        if (!node) return;
        if (priority < node->priority) refresh(node->left.get(), priority);
        else if (priority > node->priority) refresh(node->right.get(), priority);
        update(node);
    }
    static std::unique_ptr<Node> extractMinimum(std::unique_ptr<Node>& node) noexcept
    {
        if (!node->left) { auto result = std::move(node); node = std::move(result->right); return result; }
        auto result = extractMinimum(node->left); node = balance(std::move(node)); return result;
    }
    static std::unique_ptr<Node> remove(std::unique_ptr<Node> node, int priority) noexcept
    {
        if (priority < node->priority) node->left = remove(std::move(node->left), priority);
        else if (priority > node->priority) node->right = remove(std::move(node->right), priority);
        else {
            if (!node->left) return std::move(node->right);
            if (!node->right) return std::move(node->left);
            auto next = extractMinimum(node->right);
            next->left = std::move(node->left); next->right = std::move(node->right); node = std::move(next);
        }
        return balance(std::move(node));
    }
    static Entry* eligible(Node* node, double now) noexcept
    {
        if (!node || node->minimum > now) return nullptr;
        if (node->right && node->right->minimum <= now) return eligible(node->right.get(), now);
        if (!node->items.empty() && node->items.front()->deadline <= now) return node->items.front();
        return eligible(node->left.get(), now);
    }
    static bool earlier(const Entry* a, const Entry* b) noexcept
    {
        return a->deadline < b->deadline || (a->deadline == b->deadline && a->order < b->order);
    }
    static void swap(std::vector<Entry*>& items, std::size_t a, std::size_t b) noexcept
    {
        std::swap(items[a], items[b]); items[a]->position = a; items[b]->position = b;
    }
    static void up(std::vector<Entry*>& items, std::size_t index) noexcept
    {
        while (index && earlier(items[index], items[(index - 1) / 2])) { const auto parent = (index - 1) / 2; swap(items,index,parent); index=parent; }
    }
    static void down(std::vector<Entry*>& items, std::size_t index) noexcept
    {
        for (;;) {
            std::size_t child = index * 2 + 1;
            if (child >= items.size()) return;
            if (child + 1 < items.size() && earlier(items[child + 1], items[child])) ++child;
            if (!earlier(items[child], items[index])) return;
            swap(items,index,child); index=child;
        }
    }
    std::unique_ptr<Node> m_root;
    std::uint64_t m_order = 0;
};

} } // namespace content::task_scheduling
#endif
