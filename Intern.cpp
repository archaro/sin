#include "Intern.h"

// Boost serialisation functions to serialise tsl::robin_map
namespace boost { namespace serialization {
    template<class Archive, class Key, class T>
    void serialize(Archive & ar, tsl::robin_map<Key, T>& map, const unsigned int version) {
        split_free(ar, map, version);
    }

    template<class Archive, class Key, class T>
    void save(Archive & ar, const tsl::robin_map<Key, T>& map, const unsigned int /*version*/) {
        auto serializer = [&ar](const auto& v) { ar & v; };
        map.serialize(serializer);
    }

    template<class Archive, class Key, class T>
    void load(Archive & ar, tsl::robin_map<Key, T>& map, const unsigned int /*version*/) {
        auto deserializer = [&ar]<typename U>() { U u; ar & u; return u; };
        map = tsl::robin_map<Key, T>::deserialize(deserializer);
    }
}}

template<class Archive>
void String::serialize(Archive & ar, const unsigned int version) {
  ar & pagenum;
  ar & offset;
  ar & hash;
}

Page::Page (Page&& other): space(other.space), nextfree(other.nextfree), size(other.size) {
  // We've moved this block of memory so don't try to reference or close it
  other.space = NULL;
}

Page::Page(uint16_t s): size(s) {
  space = (char *)calloc(s, sizeof(char *));
  nextfree = 0;
}

Page::~Page() {
  if (space)
    free(space);
}

uint16_t Page::free_space() {
  return size - nextfree;
}

template<class Archive>
void Page::serialize(Archive & ar, const unsigned int version) {
  ar & size;
  ar & nextfree;
  using boost::serialization::make_binary_object;
  if (Archive::is_loading::value) {
    space = (char *)malloc(size);
  }
  ar & boost::serialization::make_binary_object(space, size);
}

const String& Intern::insert(const char *str) {
  // Return the String object
  const uint64_t h = hash(str);
  // Only insert the string if it isn't already there.
  // Otherwise the value of h maps to the string already interned
  const auto &found = map.find(h);
  if (found != map.end()) {
    // We have seen this string already.
    return found->second;
  } else {
    uint16_t page = get_page_index(strlen(str) + 1);
    uint16_t offset = intern_string(page, str);
    String newstr(page, offset, h);
    const auto &found = map.insert({h, newstr});
    return found.first->second;
  }
}

char *Intern::find(const uint64_t hash) {
  // Given a hash value, return its matching string.
  // If no matching string was found, return an empty string.
  const auto &result = map.find(hash);
  if (result == map.end()) {
    return emptystring;
  } else {
    return pages[result->second.pagenum].space+result->second.offset;
  }
}
uint64_t Intern::hash(const char *str) {
  return XXH3_64bits(str, strlen(str));
}

void Intern::serialise(const std::string filename = "strings.dat") {
  // Given a filename, backup the string database
  std::ofstream ofs;
  ofs.exceptions(ofs.badbit | ofs.failbit);
  ofs.open(filename, std::ios::binary);
  boost::iostreams::filtering_ostream fo;
  fo.push(boost::iostreams::zlib_compressor());
  fo.push(ofs);
  boost::archive::binary_oarchive oa(fo);
  oa << map;
  oa << pages;
}

void Intern::unserialise(const std::string filename = "strings.dat") {
  // Import a string intern database
  // THIS MUST ONLY BE DONE WITH AN EMPTY Intern OBJECT!
  std::ifstream ifs;
  ifs.exceptions(ifs.badbit | ifs.failbit | ifs.eofbit);
  ifs.open(filename, std::ios::binary);
  boost::iostreams::filtering_istream fi;
  fi.push(boost::iostreams::zlib_decompressor());
  fi.push(ifs);
  boost::archive::binary_iarchive ia(fi);
  ia >> map;
  ia >> pages;
}

uint16_t Intern::allocate_page() {
  // Create a new page, and return its index in the vector
  Page newpage(pagesize);
  pages.push_back(std::move(newpage));
  return (pages.end() - 1) - pages.begin();
}

uint16_t Intern::get_page_index(uint16_t strsize) {
  // Return the index of the first page which has the requested amount
  // of free space.  If no pages are available, create a new one and
  // return a ref to that.
  auto index = 0;
  for (auto &p: pages) {
    if (p.free_space() >= strsize)
      return index;
    index++;
  }
  return allocate_page();
}

uint16_t Intern::intern_string(const uint16_t pidx, const std::string str) {
  // Inserts the string into pages[pidx]
  // Note that this function assumes that there is enough space!
  // pidx should be calculated by calling get_page_index()
  uint16_t len = str.length() + 1; // Remember the \0
  memcpy(pages[pidx].space + pages[pidx].nextfree, str.c_str(), len);
  uint16_t offset = pages[pidx].nextfree;
  pages[pidx].nextfree += len;
  return offset;
}
