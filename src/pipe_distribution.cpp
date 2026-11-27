#include "production_network_internal.hpp"

#include <numeric>
#include <stdexcept>

namespace aic
{
namespace
{
struct PipeRequest
{
    int destination;
    double rate;
    bool controlled = false;
};
} // namespace

void add_pipe_distribution(const ProductionPlan &plan, FactoryNetwork &network,
                           std::map<std::string, int> &counts)
{
    auto &entities = network.entities;
    const auto phase = [&](const std::string &item) { return planned_phase(plan, item); };
    std::map<std::string, std::vector<PipeRequest>> pipe_requests;
    for (const auto &entity : entities)
        for (const auto &[item, rate] : entity.input_rates)
        {
            const auto medium = phase(item);
            if (medium == MaterialPhase::Solid)
                continue;
            double outstanding = rate;
            if (entity.activation && entity.activation->item == item)
            {
                pipe_requests[item].push_back({entity.id, entity.activation->rate, true});
                outstanding -= entity.activation->rate;
            }
            const double capacity = transport_capacity(medium);
            while (outstanding > 1e-9)
            {
                const double lane = std::min(outstanding, capacity);
                pipe_requests[item].push_back(
                    {entity.id, lane, entity.climate_feed || entity.projection});
                outstanding -= lane;
            }
        }
    const auto router_node = [&](const Facility *facility, const std::string &item, double rate)
    {
        if (!facility)
            throw std::invalid_argument("No enabled pipe splitter or converger for " + item);
        add_factory_consumer(network, counts, *facility, item, rate, phase(item));
        auto &entity = entities.back();
        entity.outputs = facility->outputs;
        entity.output_rates[item] = rate;
        bind_ports(RecipeProduct{item, rate, phase(item), {}}, entity.outputs, entity.output_ports);
        return entity.id;
    };
    std::map<std::string, std::vector<PipeRequest>> manufactured;
    for (const auto &entity : entities)
        for (const auto &[item, rate] : entity.output_rates)
            if (phase(item) != MaterialPhase::Solid)
                manufactured[item].push_back({entity.id, rate});
    for (const auto &[item, requests] : pipe_requests)
    {
        if (!plan.area)
            throw std::invalid_argument("Missing configured area source for " + item);
        std::vector<const AreaSource *> sites;
        for (const auto &site : plan.area->sources)
            if (site.item == item)
                sites.push_back(&site);
        std::stable_sort(sites.begin(), sites.end(),
                         [](const auto *a, const auto *b) { return a->limit > b->limit; });
        std::vector<PipeRequest> producers;
        double external = plan.external_rates.count(item) ? plan.external_rates.at(item) : 0;
        for (const auto *site : sites)
        {
            if (external <= 1e-9)
                break;
            const auto &facility = *plan.source_facilities.at(site->facility);
            const double produced = std::min(external, site->limit / 60);
            FactoryEntity source;
            source.id = entities.size();
            source.name = facility.name + "_" + std::to_string(++counts[facility.name]);
            source.type = facility.name;
            source.base_w = facility.width;
            source.base_h = facility.height;
            source.needs_power = facility.needs_power;
            source.outputs = facility.outputs;
            source.site = site;
            source.item_produced = item;
            source.output_rates[item] = produced;
            bind_ports(RecipeProduct{item, produced, phase(item), {}}, source.outputs,
                       source.output_ports);
            producers.push_back({source.id, produced});
            entities.push_back(std::move(source));
            external -= produced;
        }
        if (external > 1e-9)
            throw std::invalid_argument("Insufficient area supply of " + item + " in " +
                                        plan.area->name);
        const auto &local_producers = manufactured[item];
        producers.insert(producers.end(), local_producers.begin(), local_producers.end());
        size_t next_producer = 0;
        std::map<int, std::vector<PipeRequest>> branches;
        for (const auto &request : requests)
        {
            double remaining = request.rate;
            std::vector<std::pair<int, double>> supplied;
            while (remaining > 1e-9)
            {
                if (next_producer == producers.size())
                    throw std::invalid_argument("Insufficient pipe production of " + item);
                auto &producer = producers[next_producer];
                const double amount = std::min(producer.rate, remaining);
                supplied.emplace_back(producer.destination, amount);
                producer.rate -= amount;
                remaining -= amount;
                if (producer.rate <= 1e-9)
                    ++next_producer;
            }

            // Combine partial supplies before the destination uses a single physical port.
            while (supplied.size() > 3)
            {
                const double combined =
                    supplied[0].second + supplied[1].second + supplied[2].second;
                const int merge = router_node(plan.pipe_converger, item, combined);
                for (size_t index = 0; index < 3; ++index)
                    branches[supplied[index].first].push_back({merge, supplied[index].second});
                supplied.erase(supplied.begin(), supplied.begin() + 3);
                supplied.emplace_back(merge, combined);
            }
            int receiver = request.destination;
            if (request.controlled)
            {
                receiver = router_node(plan.pipe_valve, item, request.rate);
                entities[receiver].pipe_limit_per_minute = request.rate * 60;
                branches[receiver].push_back(request);
            }
            if (supplied.size() > 1)
            {
                const int converger = router_node(plan.pipe_converger, item, request.rate);
                branches[converger].push_back({receiver, request.rate});
                receiver = converger;
            }
            for (const auto &[id, amount] : supplied)
                branches[id].push_back({receiver, amount});
        }

        // Add a tree of physical splitters when one outlet feeds multiple pipes.
        for (auto it = branches.begin(); it != branches.end(); ++it)
        {
            const int origin = it->first;
            auto &receivers = it->second;
            if (receivers.size() <= entities[origin].output_ports.at(item).size())
            {
                for (const auto &receiver : receivers)
                    entities[origin].pipe_destinations[item].insert(receiver.destination);
                continue;
            }
            const double total = std::accumulate(receivers.begin(), receivers.end(), 0.0,
                                                 [](double sum, const PipeRequest &request)
                                                 { return sum + request.rate; });
            int splitter = router_node(plan.pipe_splitter, item, total);
            entities[origin].pipe_destinations[item].insert(splitter);
            for (size_t index = 0; index < receivers.size(); ++index)
            {
                if (index % 2 == 0 && index != 0 && receivers.size() - index > 1)
                {
                    const double tail = std::accumulate(
                        receivers.begin() + index, receivers.end(), 0.0,
                        [](double sum, const PipeRequest &request) { return sum + request.rate; });
                    const int next = router_node(plan.pipe_splitter, item, tail);
                    entities[splitter].pipe_destinations[item].insert(next);
                    splitter = next;
                }
                entities[splitter].pipe_destinations[item].insert(receivers[index].destination);
            }
        }
    }
}
} // namespace aic
