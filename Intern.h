#pragma once

#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/binary_object.hpp>
#include <fstream>

#include "tsl/robin_map.h"
#include "xxhash.h"

class Page {
  // A page of memory for storing strings in:
  //   char *space = the block of memory to store strings in
  //   uint16_t size = the size of the block
  //   uint16_t nextfree = the offset to the next free byte
  // This may all be a bit overengineered, and it may work out just as
  // performant to intern strings directly into the hash table using
  // std::Strings, but we'll see.
  private:
    // Copying Pages is not allowed.  Verboten.
    Page (const Page&) = delete;
    Page& operator= (const Page&) = delete;
    friend class boost::serialization::access;
    // A null constructor is only used for serialisation.
    Page(): size(0u), nextfree(0u), space(nullptr) {}
    // When the class Archive corresponds to an output archive, the
    // & operator is defined similar to <<.  Likewise, when the class Archive
    // is a type of input archive the & operator is defined similar to >>.
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version);
  public:
    Page (Page&& other);
    Page(uint16_t s);
    ~Page();
    uint16_t free_space();
    uint16_t size;
    uint16_t nextfree;
    char *space;
};

class String {
  // An object containing the important information necessary to find
  // a string:
  //   uint64_t pagenum = the index into the vector of pages
  //   uint64_t offset = the offest from the start of the page
  //   uint64_t hash = the hash value of this string
  public:
    uint16_t pagenum;
    uint16_t offset;
    uint64_t hash;
    String(const uint16_t pg, const uint16_t off, const uint64_t h):
      pagenum(pg), offset(off), hash(h) {}
    // A dummy String.  Only used when it is necessary to return a String
    // object even when it will not be used.  Wicked, bad and wrong.
    String(): pagenum(0), offset(0), hash(0) {}
  private:
    friend class boost::serialization::access;
    // When the class Archive corresponds to an output archive, the
    // & operator is defined similar to <<.  Likewise, when the class Archive
    // is a type of input archive the & operator is defined similar to >>.
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version);
};

class Intern {
  // A simple string-interning class.  A block of space is reserved for the
  // insertion of strings, and a hashtable is created which maps the hash
  // of the string to its offset in the block of space.
  // There is currently no way to delete or update a string once it has
  // been interned.
  public:
    const String& insert(const char *str);
    char *find(const uint64_t hash);
    static uint64_t hash(const char *str);
    void serialise(const std::string filename);
    void unserialise(const std::string filename);
  private:
    inline static char emptystring[] = "";
    tsl::robin_map<uint64_t, String> map;
    std::vector<Page> pages;
    const uint64_t pagesize = 32768; // 32k per string page
    char *nextspace;
    uint16_t allocate_page();
    uint16_t get_page_index(uint16_t strsize);
    uint16_t intern_string(const uint16_t pidx, const std::string str);
};

