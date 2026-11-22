#include "dvv/registry/component_registry.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <ranges>
#include <string>

#include "dvv/base/log.h"

namespace dvv {

namespace detail {

constinit RegistrationNode* RegistrationNode::list_head = nullptr;

RegistrationNode::RegistrationNode(const std::type_info& interface,
                                   RegistrationHooks hooks) noexcept
    : interface_(&interface), hooks_(hooks), next_(list_head) {
    list_head = this;
}

ModuleHandles::~ModuleHandles() {
    while (!handles_.empty()) {
        handles_.pop_back();
    }
}

void ModuleHandles::add(std::shared_ptr<void> handle) {
    if (handle != nullptr) {
        handles_.push_back(std::move(handle));
    }
}

}  // namespace detail

namespace {

std::string internalReason(std::string_view what, std::string_view detail) {
    return std::format("{}: {}: {}", errcName(Errc::Internal), what, detail);
}

// Calls on_run(first, last) for each run of two or more entries with the same id in `entries`,
// which is sorted by id_of.
template <class Entries, class IdOf, class OnRun>
void forEachDuplicateRun(Entries& entries, IdOf id_of, OnRun on_run) {
    for (auto first = std::ranges::begin(entries); first != std::ranges::end(entries);) {
        const std::string_view id = id_of(*first);
        const auto last = std::ranges::find_if(
            first, std::ranges::end(entries), [id](std::string_view other) { return other != id; },
            id_of);
        if (std::distance(first, last) > 1) {
            on_run(first, last);
        }
        first = last;
    }
}

// The status of a module whose load() returned `result`: provides and withheld follow
// info.produces, each name once.
ModuleStatus moduleStatus(const ModuleInfo& info, ModuleLoad result) {
    ModuleStatus status{.info = info};
    for (const std::string_view name : info.produces) {
        if (std::ranges::contains(status.provides, name) ||
            std::ranges::contains(status.withheld, name)) {
            continue;
        }
        if (std::ranges::contains(result.provides, name)) {
            status.provides.emplace_back(name);
        } else {
            status.withheld.emplace_back(name);
        }
    }
    // An empty reason means "complete".
    if (!status.withheld.empty()) {
        status.reason = std::move(result.reason);
    }
    status.properties = std::move(result.properties);
    return status;
}

// The status of a module that provides nothing because of `reason`; never empty, also when the
// module produces nothing.
ModuleStatus failedStatus(const ModuleInfo& info, std::string reason,
                          std::vector<ModuleProperty> properties = {}) {
    ModuleStatus status = moduleStatus(info, ModuleLoad{.properties = std::move(properties)});
    status.reason = std::move(reason);
    return status;
}

// Runs one module's load() and adds the state it returns to `handles` at once, whatever it
// provides. The log lines name the module and capabilities only: the exception text, the reason
// and the properties may hold a path, so they go only into the status.
ModuleStatus loadModule(ModuleLoad (*load)(const ModuleValues&), const ModuleInfo& info,
                        const ModuleValues& values, detail::ModuleHandles& handles) {
    ModuleLoad result;
    try {
        result = load(values);
    } catch (...) {
        DVV_LOG_ERROR("module '{}' threw from load()", info.id);
        return failedStatus(info, internalReason("load() threw", dvv::describeCurrentException()));
    }
    handles.add(std::move(result.handle));
    const auto undeclared = std::ranges::find_if(result.provides, [&info](const std::string& name) {
        return !std::ranges::contains(info.produces, std::string_view(name));
    });
    if (undeclared != result.provides.end()) {
        DVV_LOG_ERROR("module '{}' provided '{}', which its info() does not list", info.id,
                      *undeclared);
        return failedStatus(info,
                            std::format("{}: load() provided undeclared capability '{}'",
                                        errcName(Errc::Internal), *undeclared),
                            std::move(result.properties));
    }
    return moduleStatus(info, std::move(result));
}

// Logs what a module provides and withholds: ids and capability names only.
void logModuleStatus(const ModuleStatus& status) {
    const auto join = [](const std::vector<std::string>& names) {
        std::string joined;
        for (const std::string& name : names) {
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += name;
        }
        return joined;
    };
    const std::string provides = status.provides.empty() ? "nothing" : join(status.provides);
    if (status.withheld.empty()) {
        DVV_LOG_INFO("module '{}': provides {}", status.info.id, provides);
    } else {
        DVV_LOG_INFO("module '{}': provides {}; withholds {}", status.info.id, provides,
                     join(status.withheld));
    }
}

}  // namespace

std::unique_ptr<ComponentRegistry> ComponentRegistry::create(const Capabilities& caps,
                                                             std::source_location where) {
    Parts parts{.entries = probeComponents(caps), .modules = {}, .capabilities = caps};
    return std::make_unique<ComponentRegistry>(
        CreationKey<ComponentRegistry>(
            LifetimeRegistry::instance().issue<ComponentRegistry>(NO_OWNER, where)),
        std::move(parts));
}

std::unique_ptr<ComponentRegistry> ComponentRegistry::load(const ModuleOptions& options,
                                                           std::source_location where) {
    struct Registered {
        const detail::ModuleRegistrationNode* node;
        ModuleInfo info;
        bool duplicated;
    };
    std::vector<Registered> registered;
    for (const detail::ModuleRegistrationNode* node = detail::ModuleRegistrationNode::list_head;
         node != nullptr; node = node->next_) {
        registered.push_back({.node = node, .info = node->info_(), .duplicated = false});
    }
    std::ranges::stable_sort(registered, {},
                             [](const Registered& module) { return module.info.id; });

    // Registration order across translation units is unspecified and a load has side effects, so
    // no copy of a duplicated id runs.
    forEachDuplicateRun(
        registered, [](const Registered& module) { return module.info.id; },
        [](auto first, auto last) {
            DVV_LOG_ERROR("module id '{}' is registered more than once", first->info.id);
            for (auto copy = first; copy != last; ++copy) {
                copy->duplicated = true;
            }
        });

    // A module switched off at build time is not an error.
    for (const std::string& id : options.modules | std::views::keys) {
        if (!std::ranges::contains(registered, std::string_view(id),
                                   [](const Registered& module) { return module.info.id; })) {
            DVV_LOG_WARN("module options for '{}': no such module", id);
        }
    }

    Parts parts;
    const ModuleValues no_values;
    for (const Registered& module : registered) {
        ModuleStatus status;
        if (module.duplicated) {
            status = failedStatus(
                module.info, internalReason("duplicate module id", std::string(module.info.id)));
        } else {
            const auto found = options.modules.find(module.info.id);
            const ModuleValues& values = found != options.modules.end() ? found->second : no_values;
            status = loadModule(module.node->load_, module.info, values, parts.handles);
        }
        logModuleStatus(status);
        parts.modules.push_back(std::move(status));
    }
    for (const ModuleStatus& status : parts.modules) {
        for (const std::string& name : status.provides) {
            parts.capabilities.add(name);
        }
    }
    parts.entries = probeComponents(parts.capabilities);

    return std::make_unique<ComponentRegistry>(
        CreationKey<ComponentRegistry>(
            LifetimeRegistry::instance().issue<ComponentRegistry>(NO_OWNER, where)),
        std::move(parts));
}

std::vector<ComponentRegistry::Entry> ComponentRegistry::probeComponents(const Capabilities& caps) {
    std::vector<Entry> entries;
    for (const detail::RegistrationNode* node = detail::RegistrationNode::list_head;
         node != nullptr; node = node->next_) {
        Entry entry{node, ComponentStatus{.info = node->hooks_.info()}};
        ComponentStatus& status = entry.status;
        try {
            if (auto probed = node->hooks_.probe(caps); !probed) {
                status.unavailable = std::move(probed.error());
            }
        } catch (...) {
            status.unavailable =
                Unavailable{internalReason("probe() threw", dvv::describeCurrentException())};
        }
        if (const auto& unavailable = status.unavailable) {
            DVV_LOG_INFO("component '{}' is unavailable: {}", status.info.id, unavailable->reason);
        }
        entries.push_back(std::move(entry));
    }

    std::ranges::stable_sort(entries, {}, [](const Entry& entry) { return entry.status.info.id; });
    // Registration order across translation units is unspecified, so no copy of a duplicated id
    // may win: all of them are unavailable.
    forEachDuplicateRun(
        entries, [](const Entry& entry) { return entry.status.info.id; },
        [](auto first, auto last) {
            DVV_LOG_ERROR("component id '{}' is registered more than once", first->status.info.id);
            for (auto entry = first; entry != last; ++entry) {
                entry->status.unavailable = Unavailable{
                    internalReason("duplicate component id", std::string(first->status.info.id))};
            }
        });
    return entries;
}

ComponentRegistry::ComponentRegistry(CreationKey<ComponentRegistry> key, Parts parts) noexcept
    : Managed(std::move(key)),
      entries_(std::move(parts.entries)),
      modules_(std::move(parts.modules)),
      capabilities_(std::move(parts.capabilities)),
      handles_(std::move(parts.handles)) {}

std::span<const ModuleStatus> ComponentRegistry::modules() const noexcept {
    return modules_;
}

const ModuleStatus* ComponentRegistry::module(std::string_view id) const noexcept {
    const auto found =
        std::ranges::find(modules_, id, [](const ModuleStatus& status) { return status.info.id; });
    return found != modules_.end() ? &*found : nullptr;
}

const Capabilities& ComponentRegistry::capabilities() const noexcept {
    return capabilities_;
}

std::vector<ComponentStatus> ComponentRegistry::list() const {
    std::lock_guard lock(mutex_);
    std::vector<ComponentStatus> statuses;
    statuses.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        statuses.push_back(entry.status);
    }
    return statuses;
}

std::vector<ComponentStatus> ComponentRegistry::listFor(const std::type_info& interface) const {
    std::lock_guard lock(mutex_);
    std::vector<ComponentStatus> statuses;
    for (const Entry& entry : entries_) {
        if (*entry.registration->interface_ == interface) {
            statuses.push_back(entry.status);
        }
    }
    return statuses;
}

Result<const detail::RegistrationNode*> ComponentRegistry::prepare(
    std::string_view id, const std::type_info& interface) const {
    std::lock_guard lock(mutex_);
    const auto found =
        std::ranges::find(entries_, id, [](const Entry& entry) { return entry.status.info.id; });
    if (found == entries_.end()) {
        return makeError(Errc::Unsupported, std::format("unknown component '{}'", id));
    }
    if (*found->registration->interface_ != interface) {
        return makeError(Errc::Unsupported, std::format("component '{}' does not implement {}", id,
                                                        detail::demangle(interface.name())));
    }
    if (const auto& unavailable = found->status.unavailable) {
        return makeError(Errc::Unsupported,
                         std::format("component '{}' is unavailable: {}", id, unavailable->reason));
    }
    return found->registration;
}

Error ComponentRegistry::constructionFailed(std::string_view id) {
    const std::string what = dvv::describeCurrentException();
    DVV_LOG_ERROR("component '{}' threw from its constructor and is disabled: {}", id, what);
    {
        std::lock_guard lock(mutex_);
        for (Entry& entry : entries_) {
            if (entry.status.info.id == id) {
                entry.status.unavailable = Unavailable{internalReason("constructor threw", what)};
            }
        }
    }
    return makeError(Errc::Internal, std::format("component '{}' failed: {}", id, what)).error();
}

}  // namespace dvv
