#pragma once

#include <memory>

namespace cpf {
   /// Alias for an owning unique pointer.
   template<typename T> using uptr = std::unique_ptr<T>;
   /// Alias for a shared ownership pointer.
   template<typename T> using sptr = std::shared_ptr<T>;
   /// Alias for a non-owning weak pointer.
   template<typename T> using wptr = std::weak_ptr<T>;
} // namespace cpf
