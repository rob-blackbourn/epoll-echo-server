#ifndef JETBLACK_IO_MATCH_HPP
#define JETBLACK_IO_MATCH_HPP

// For rust style matching with variants.

namespace jetblack::utils
{
  
  template<class... Ts> struct match : Ts... { using Ts::operator()...; };
  template<class... Ts> match(Ts...) -> match<Ts...>;

}

#endif // JETBLACK_IO_MATCH_HPP
