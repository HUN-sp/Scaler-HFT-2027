#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <list>
#include <cstdint>
#include <chrono>
#include <iomanip>
#include <functional> // For std::greater

// Forward declaration
struct Order;

struct Limit {
    uint64_t total_quantity = 0;
    std::list<Order*> orders;
};

struct Order {
    uint64_t order_id;
    bool is_buy;
    double price;
    uint64_t quantity;
    uint64_t timestamp_ns;

    
    std::list<Order*>::iterator position_in_limit;
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};

// The main OrderBook class.
class OrderBook {
private:
    
    std::map<double, Limit, std::greater<double>> bids;
    std::map<double, Limit> asks;

  
    std::list<Order> order_storage;

    std::unordered_map<uint64_t, std::list<Order>::iterator> order_lookup;

    uint64_t next_order_id = 1;

public:

   uint64_t newOrder(bool is_buy, double price, uint64_t quantity) {
        // 1. Create the order and place it in our memory pool
        uint64_t current_id = next_order_id++;
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();

        // emplace_back constructs the Order in place, avoiding copies.
        order_storage.emplace_back(Order{current_id, is_buy, price, quantity, (uint64_t)timestamp});
        
        std::list<Order>::iterator order_it = --order_storage.end();

        order_lookup[current_id] = order_it;

        if (is_buy) {
            add_order_internal(bids, order_it);
        } else {
            add_order_internal(asks, order_it);
        }

        return current_id;
    }

   
    bool cancelOrder(uint64_t order_id) {
        auto it = order_lookup.find(order_id);
        if (it == order_lookup.end()) {
            return false; // Order not found
        }
        
        std::list<Order>::iterator order_it = it->second;

        if (order_it->is_buy) {
            remove_order_internal(bids, order_it);
        } else {
            remove_order_internal(asks, order_it);
        }

        order_lookup.erase(it);
        order_storage.erase(order_it);

        return true;
    }

    
    bool amendOrder(uint64_t order_id, double new_price, uint64_t new_quantity) {
        auto it = order_lookup.find(order_id);
        if (it == order_lookup.end()) {
            return false; // Order not found
        }
        
        std::list<Order>::iterator order_it = it->second;

        if (order_it->price != new_price) {
            if (order_it->is_buy) {
                remove_order_internal(bids, order_it);
            } else {
                remove_order_internal(asks, order_it);
            }

            order_it->price = new_price;
            order_it->quantity = new_quantity;

            if (order_it->is_buy) {
                add_order_internal(bids, order_it);
            } else {
                add_order_internal(asks, order_it);
            }
        } else { 
            uint64_t old_quantity = order_it->quantity;
            int64_t quantity_delta = new_quantity - old_quantity;
            
            order_it->quantity = new_quantity;

            if (order_it->is_buy) {
                bids[order_it->price].total_quantity += quantity_delta;
            } else {
                asks[order_it->price].total_quantity += quantity_delta;
            }
        }
        return true;
    }

    
    void getSnapshot(size_t depth, std::vector<PriceLevel>& out_bids, std::vector<PriceLevel>& out_asks) const {
        out_bids.clear();
        out_asks.clear();
        out_bids.reserve(depth);
        out_asks.reserve(depth);

        // Get top N bids (highest prices)
        for (const auto& pair : bids) {
            if (out_bids.size() >= depth) break;
            out_bids.push_back({pair.first, pair.second.total_quantity});
        }

        // Get top N asks (lowest prices)
        for (const auto& pair : asks) {
            if (out_asks.size() >= depth) break;
            out_asks.push_back({pair.first, pair.second.total_quantity});
        }
    }
    
    
    void printBook(size_t depth = 10) const {
        std::vector<PriceLevel> snap_bids, snap_asks;
        getSnapshot(depth, snap_bids, snap_asks);

        std::cout << "--- ORDER BOOK ---" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        
        std::cout << "ASKS (Price | Quantity)" << std::endl;
        std::cout << "----------------------" << std::endl;
        for (auto it = snap_asks.rbegin(); it != snap_asks.rend(); ++it) {
            std::cout << it->price << " | " << it->total_quantity << std::endl;
        }

        std::cout << "----------------------" << std::endl;

        std::cout << "BIDS (Price | Quantity)" << std::endl;
        std::cout << "----------------------" << std::endl;
        for (const auto& level : snap_bids) {
            std::cout << level.price << " | " << level.total_quantity << std::endl;
        }
        std::cout << "----------------------" << std::endl << std::endl;
    }
    
    
    std::pair<PriceLevel, PriceLevel> getBBO() const {
        PriceLevel best_bid = {0, 0};
        PriceLevel best_ask = {0, 0};

        if (!bids.empty()) {
            const auto& top_bid = *bids.begin();
            best_bid = {top_bid.first, top_bid.second.total_quantity};
        }

        if (!asks.empty()) {
            const auto& top_ask = *asks.begin();
            best_ask = {top_ask.first, top_ask.second.total_quantity};
        }

        return {best_bid, best_ask};
    }

private:
    // --- Internal Helper Methods ---

    
    template<typename T>
    void add_order_internal(T& book_side, std::list<Order>::iterator order_it) {
        double price = order_it->price;
        Limit& limit = book_side[price]; 

        limit.orders.push_back(&(*order_it));
        limit.total_quantity += order_it->quantity;

        order_it->position_in_limit = --limit.orders.end();
    }
    
    
    template<typename T>
    void remove_order_internal(T& book_side, std::list<Order>::iterator order_it) {
        double price = order_it->price;
        auto limit_it = book_side.find(price);

        if (limit_it != book_side.end()) {
            Limit& limit = limit_it->second;
            limit.total_quantity -= order_it->quantity;
            
            // O(1) removal from the list using the stored iterator
            limit.orders.erase(order_it->position_in_limit);
            
            // If the price level is now empty, remove it from the map
            if (limit.total_quantity == 0) {
                book_side.erase(limit_it);
            }
        }
    }
};

int main() {
    OrderBook book;

    std::cout << "--- Initial Empty Book ---" << std::endl;
    book.printBook();

    // Add some orders
    uint64_t order1 = book.newOrder(true, 100.0, 10);  // BUY
    uint64_t order2 = book.newOrder(true, 100.0, 5);   // BUY
    uint64_t order3 = book.newOrder(true, 99.0, 20);   // BUY
    uint64_t order4 = book.newOrder(false, 100.0, 15); // SELL
    uint64_t order5 = book.newOrder(false, 102.0, 10); // SELL
    uint64_t order6 = book.newOrder(false, 101.0, 5);  // SELL

    std::cout << "--- Book After Adding Orders ---" << std::endl;
    book.printBook();

    // Get BBO
    auto bbo = book.getBBO();
    std::cout << "Best Bid: " << bbo.first.price << " | Qty: " << bbo.first.total_quantity << std::endl;
    std::cout << "Best Ask: " << bbo.second.price << " | Qty: " << bbo.second.total_quantity << std::endl << std::endl;

    // Cancel an order
    std::cout << "--- Cancelling Order ID: " << order2 << " (BUY 5 @ 100.0) ---" << std::endl;
    book.cancelOrder(order2);
    book.printBook();

    // Amend an order (quantity only)
    std::cout << "--- Amending Order ID: " << order3 << " from 20 to 25 @ 99.0 ---" << std::endl;
    book.amendOrder(order3, 99.0, 25);
    book.printBook();

    // Amend an order (price change)
    std::cout << "--- Amending Order ID: " << order5 << " from 10 @ 102.0 to 10 @ 100.5 ---" << std::endl;
    book.amendOrder(order5, 100.5, 10); // Now a SELL at 100.5
    book.printBook();

    return 0;
}
