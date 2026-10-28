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
                                   std::optional<Kind> interface_kind, InfoFunction info,
                                   SchemaFunction schema, ProbeFunction probe) noexcept
    : interface_(&interface),
      interface_kind_(interface_kind),
      info_(info),
      schema_(schema),
      probe_(probe),
      next_(list_head) {
    list_head = this;
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

}  // namespace

std::unique_ptr<ComponentRegistry> ComponentRegistry::create(const Capabilities& caps) {
    std::vector<Entry> entries;
    for (const detail::RegistrationNode* node = detail::RegistrationNode::list_head;
         node != nullptr; node = node->next_) {
        Entry entry{node, ComponentStatus{.info = node->info_()}};
        ComponentStatus& status = entry.status;
        try {
            status.schema = &node->schema_();
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
                if (auto probed = node->probe_(caps); !probed) {
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

    return std::make_unique<ComponentRegistry>(
        CreationKey<ComponentRegistry>(LifetimeRegistry::instance().issue<ComponentRegistry>()),
        std::move(entries));
}

ComponentRegistry::ComponentRegistry(CreationKey<ComponentRegistry> key,
                                     std::vector<Entry> entries) noexcept
    : Managed(std::move(key)), entries_(std::move(entries)) {}

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
