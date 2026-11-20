#include "dvv/registry/component_registry.h"

#include <algorithm>
#include <exception>
#include <format>
#include <iterator>
#include <string>

#include "dvv/base/log.h"

namespace dvv {

namespace detail {

constinit RegistrationNode* RegistrationNode::list_head = nullptr;

RegistrationNode::RegistrationNode(const std::type_info& interface,
                                   std::optional<Kind> interface_kind,
                                   RegistrationHooks hooks) noexcept
    : interface_(&interface), interface_kind_(interface_kind), hooks_(hooks), next_(list_head) {
    list_head = this;
}

const ParameterSchema& noParameters() noexcept {
    static const ParameterSchema NONE;
    return NONE;
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

// Describes the exception being handled; call only from a catch block.
std::string describeCurrentException() {
    try {
        throw;
    } catch (const std::bad_expected_access<Error>& error) {
        // A failed Result<T>::value(), e.g. ParameterSchema::Builder::build().value().
        return error.error().describe();
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown exception";
    }
}

std::string internalReason(std::string_view what, std::string_view detail) {
    return std::format("{}: {}: {}", errcName(Errc::Internal), what, detail);
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
    status.facts = std::move(result.facts);
    return status;
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
    };
    std::vector<Registered> registered;
    for (const detail::ModuleRegistrationNode* node = detail::ModuleRegistrationNode::list_head;
         node != nullptr; node = node->next_) {
        registered.push_back({node, node->info_()});
    }
    std::ranges::stable_sort(registered, {},
                             [](const Registered& module) { return module.info.id; });

    Parts parts;
    const ParameterValues no_values;
    for (const Registered& module : registered) {
        const auto found = options.modules.find(module.info.id);
        const ParameterValues& values = found != options.modules.end() ? found->second : no_values;
        parts.modules.push_back(moduleStatus(module.info, module.node->load_(values)));
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
            status.schema = &node->hooks_.schema();
        } catch (...) {
            status.unavailable =
                Unavailable{internalReason("schema() threw", describeCurrentException())};
        }
        if (status.schema != nullptr && node->interface_kind_ &&
            *node->interface_kind_ != status.info.kind) {
            // Checked before probe(): a component of the wrong kind is never probed.
            status.unavailable = Unavailable{std::format("kind {} does not match interface kind {}",
                                                         kindName(status.info.kind),
                                                         kindName(*node->interface_kind_))};
        } else if (status.schema != nullptr) {
            try {
                if (auto probed = node->hooks_.probe(caps); !probed) {
                    status.unavailable = std::move(probed.error());
                }
            } catch (...) {
                status.unavailable =
                    Unavailable{internalReason("probe() threw", describeCurrentException())};
            }
        }
        if (const auto& unavailable = status.unavailable) {
            DVV_LOG_INFO("component '{}' is unavailable: {}", status.info.id, unavailable->reason);
        }
        entries.push_back(std::move(entry));
    }

    std::ranges::stable_sort(entries, {}, [](const Entry& entry) { return entry.status.info.id; });
    // Registration order across translation units is unspecified, so no copy of a duplicated id
    // may win: all of them are unavailable.
    for (auto first = entries.begin(); first != entries.end();) {
        const auto last = std::ranges::find_if(first, entries.end(), [&](const Entry& entry) {
            return entry.status.info.id != first->status.info.id;
        });
        if (std::distance(first, last) > 1) {
            DVV_LOG_ERROR("component id '{}' is registered more than once", first->status.info.id);
            for (auto entry = first; entry != last; ++entry) {
                entry->status.unavailable = Unavailable{
                    internalReason("duplicate component id", std::string(first->status.info.id))};
            }
        }
        first = last;
    }
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

std::vector<ComponentStatus> ComponentRegistry::list(Kind kind) const {
    std::lock_guard lock(mutex_);
    std::vector<ComponentStatus> statuses;
    for (const Entry& entry : entries_) {
        if (entry.status.info.kind == kind) {
            statuses.push_back(entry.status);
        }
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

Result<ComponentRegistry::Prepared> ComponentRegistry::prepare(
    std::string_view id, const std::type_info& interface, const ParameterValues& values) const {
    const detail::RegistrationNode* registration = nullptr;
    const ParameterSchema* schema = nullptr;
    {
        std::lock_guard lock(mutex_);
        const auto found = std::ranges::find(
            entries_, id, [](const Entry& entry) { return entry.status.info.id; });
        if (found == entries_.end()) {
            return makeError(Errc::Unsupported, std::format("unknown component '{}'", id));
        }
        if (*found->registration->interface_ != interface) {
            return makeError(Errc::Unsupported,
                             std::format("component '{}' does not implement {}", id,
                                         detail::demangle(interface.name())));
        }
        if (const auto& unavailable = found->status.unavailable) {
            return makeError(Errc::Unsupported, std::format("component '{}' is unavailable: {}", id,
                                                            unavailable->reason));
        }
        registration = found->registration;
        schema = found->status.schema;
    }
    auto resolved = schema->resolve(values);
    if (!resolved) {
        return makeError(Errc::Format,
                         std::format("component '{}': {}", id, resolved.error().message));
    }
    return Prepared{registration, std::move(*resolved)};
}

Error ComponentRegistry::constructionFailed(std::string_view id) {
    const std::string what = describeCurrentException();
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
