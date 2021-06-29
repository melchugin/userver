#pragma once

#include <iosfwd>
#include <tuple>
#include <type_traits>

#include <storages/postgres/detail/is_decl_complete.hpp>
#include <storages/postgres/io/io_fwd.hpp>
#include <storages/postgres/io/traits.hpp>

#include <utils/variadic_logic.hpp>

namespace storages::postgres::io::traits {

template <bool Value>
using BoolConstant = std::integral_constant<bool, Value>;
template <std::size_t Value>
using SizeConstant = std::integral_constant<std::size_t, Value>;

//@{
/** @name Type mapping traits */
/// @brief Detect if the C++ type is mapped to a Postgres system type.
template <typename T>
struct IsMappedToSystemType : utils::IsDeclComplete<CppToSystemPg<T>> {};
template <typename T>
constexpr bool kIsMappedToSystemType = IsMappedToSystemType<T>::value;

/// @brief Detect if the C++ type is mapped to a Postgres user type.
template <typename T>
struct IsMappedToUserType : utils::IsDeclComplete<CppToUserPg<T>> {};
template <typename T>
constexpr bool kIsMappedToUserType = IsMappedToUserType<T>::value;

/// @brief Detect if the C++ type is mapped to a Postgres array type.
template <typename T>
struct IsMappedToArray;
template <typename T>
constexpr bool kIsMappedToArray = IsMappedToArray<T>::value;

/// @brief Detect if the C++ type is mapped to a Postgres type.
template <typename T, typename = ::utils::void_t<>>
struct IsMappedToPg
    : BoolConstant<kIsMappedToUserType<T> || kIsMappedToSystemType<T> ||
                   kIsMappedToArray<T>> {};
template <typename T>
constexpr bool kIsMappedToPg = IsMappedToPg<T>::value;

/// @brief Mark C++ mapping a special case for disambiguation.
template <typename T, typename = ::utils::void_t<>>
struct IsSpecialMapping : std::false_type {};
template <typename T>
constexpr bool kIsSpecialMapping = IsSpecialMapping<T>::value;
//@}

//@{
/** @name Detect iostream operators */
template <typename T, typename = ::utils::void_t<>>
struct HasOutputOperator : std::false_type {};

template <typename T>
struct HasOutputOperator<T,
                         ::utils::void_t<decltype(std::declval<std::ostream&>()
                                                  << std::declval<T&>())>>
    : std::true_type {};

template <typename T, typename = ::utils::void_t<>>
struct HasInputOperator : std::false_type {};

template <typename T>
struct HasInputOperator<
    T, ::utils::void_t<decltype(std::declval<std::istream&>() >>
                                std::declval<T&>())>> : std::true_type {};
//@}

//@{
/** @name Traits for containers */
/// @brief Mark C++ container type as supported by the driver.
template <typename T>
struct IsCompatibleContainer : std::false_type {};
template <typename T>
constexpr bool kIsCompatibleContainer = IsCompatibleContainer<T>::value;

/// @brief Mark C++ container as fixed size.
/// e.g. std::arrray
template <typename T>
struct IsFixedSizeContainer : std::false_type {};
template <typename T>
constexpr bool kIsFixedSizeContainer = IsFixedSizeContainer<T>::value;

//@{
/// @brief Calculate number of dimensions in C++ container.
template <typename T, typename Enable = ::utils::void_t<>>
struct DimensionCount : SizeConstant<0> {};

template <typename T>
struct DimensionCount<T, std::enable_if_t<kIsCompatibleContainer<T>>>
    : SizeConstant<1 + DimensionCount<typename T::value_type>::value> {};
template <typename T>
constexpr std::size_t kDimensionCount = DimensionCount<T>::value;
//@}

//@{
/// @brief Detect type of multidimensional C++ container.
template <typename T>
struct ContainerFinalElement;

namespace detail {

template <typename T>
struct FinalElementImpl {
  using type = T;
};

template <typename T>
struct ContainerFinalElementImpl {
  using type = typename ContainerFinalElement<typename T::value_type>::type;
};

}  // namespace detail

template <typename T>
struct ContainerFinalElement
    : std::conditional_t<kIsCompatibleContainer<T>,
                         detail::ContainerFinalElementImpl<T>,
                         detail::FinalElementImpl<T>> {};

template <typename T>
using ContainerFinaleElementType = typename ContainerFinalElement<T>::type;
//@}

//@{
/** @name IsMappedToArray implementation */
namespace detail {

template <typename Container>
constexpr bool EnableContainerMapping() {
  if constexpr (!traits::kIsCompatibleContainer<Container>) {
    return false;
  } else {
    return traits::kIsMappedToPg<
        typename traits::ContainerFinalElement<Container>::type>;
  }
}

}  // namespace detail

template <typename T>
struct IsMappedToArray : BoolConstant<detail::EnableContainerMapping<T>()> {};

//@}

template <typename T, typename = ::utils::void_t<>>
struct CanReserve : std::false_type {};
template <typename T>
struct CanReserve<T, ::utils::void_t<decltype(std::declval<T>().reserve(
                         std::declval<std::size_t>()))>> : std::true_type {};
template <typename T>
constexpr bool kCanReserve = CanReserve<T>::value;

template <typename T, typename = ::utils::void_t<>>
struct CanResize : std::false_type {};

template <typename T>
struct CanResize<T, ::utils::void_t<decltype(std::declval<T>().resize(
                        std::declval<std::size_t>()))>> : std::true_type {};
template <typename T>
constexpr bool kCanResize = CanResize<T>::value;

template <typename T, typename = ::utils::void_t<>>
struct CanPushBack : std::false_type {};

template <typename T>
struct CanPushBack<T, ::utils::void_t<decltype(std::declval<T>().push_back(
                          std::declval<typename T::value_type>()))>>
    : std::true_type {};

template <typename T>
constexpr bool kCanPushBack = CanPushBack<T>::value;

template <typename T, typename = ::utils::void_t<>>
struct CanClear : std::false_type {};

template <typename T>
struct CanClear<T, ::utils::void_t<decltype(std::declval<T>().clear())>>
    : std::true_type {};
template <typename T>
constexpr bool kCanClear = CanClear<T>::value;

template <typename T>
auto Inserter(T& container) {
  if constexpr (kCanPushBack<T>) {
    return std::back_inserter(container);
  } else if constexpr (kIsFixedSizeContainer<T>) {
    return container.begin();
  } else {
    return std::inserter(container, container.end());
  }
}
//@}

template <typename T>
struct RemoveTupleReferences;

template <typename... T>
struct RemoveTupleReferences<std::tuple<T...>> {
  using type = std::tuple<std::remove_reference_t<T>...>;
};

template <typename T>
struct IsTupleOfRefs : std::false_type {};
template <typename... T>
struct IsTupleOfRefs<std::tuple<T&...>> : std::true_type {};

template <typename T>
struct AddTupleConstRef;

template <typename... T>
struct AddTupleConstRef<std::tuple<T...>> {
  using type = std::tuple<
      std::add_const_t<std::add_lvalue_reference_t<std::decay_t<T>>>...>;
};

template <typename T, template <typename> class Pred,
          template <typename...> class LogicOp = ::utils::And>
struct TupleIOTrait : std::false_type {};
template <typename... T, template <typename> class Pred,
          template <typename...> class LogicOp>
struct TupleIOTrait<std::tuple<T...>, Pred, LogicOp>
    : LogicOp<typename Pred<std::decay_t<T>>::type...>::type {};

template <typename T>
struct TupleHasParsers : TupleIOTrait<T, HasParser> {};
template <typename T>
struct TupleHasFormatters : TupleIOTrait<T, HasFormatter> {};

//@}

//@{
/** @name Type mapping traits */
//@}

}  // namespace storages::postgres::io::traits
