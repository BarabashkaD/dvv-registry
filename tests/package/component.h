#pragma once

class IGreeting {
public:
    IGreeting() = default;
    IGreeting(const IGreeting&) = delete;
    IGreeting& operator=(const IGreeting&) = delete;
    IGreeting(IGreeting&&) = delete;
    IGreeting& operator=(IGreeting&&) = delete;
    virtual ~IGreeting() = default;

    [[nodiscard]] virtual const char* text() const = 0;
};
