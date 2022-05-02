#pragma once

#include <fmt/core.h>

#include <iostream>

auto& errs() {
  return std::cerr;
}

template<typename... Ts>
constexpr void errsv(fmt::format_string<Ts...> Fmt, Ts&&... T) {
  errs() << fmt::format(Fmt, std::forward<Ts>(T)...) << std::endl;
}
