/*
   copyright 20016-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.
*/
#include "config.h"

#include "Fileinfo.hh"              //file container
#include "MultiAttributeCompare.hh" //for sorting on multiple attributes
#include "Rdutil.hh"
#include "algos.hh" //to find duplicates in a vector
#include <algorithm>
#include <cassert>
#include <fstream> //for file writing
#include <ostream> //for output
#include <string>  //for easier passing of string arguments
#include <time.h>  //to be able to call nanosleep properly.
#include <tuple>

int
Rdutil::printtofile(const std::string& filename) const
{
  // open a file to print to
  std::ofstream f1;
  f1.open(filename.c_str(), std::ios_base::out);
  if (!f1.is_open()) {
    std::cerr << "could not open file \"" << filename << "\"\n";
    return -1;
  }

  // exchange f1 for cout to write to terminal instead of file
  std::ostream& output(f1);

  // This uses "priority" instead of "cmdlineindex". Change this the day
  // a change in output format is allowed (for backwards compatibility).
  output << "# Automatically generated\n";
  output << "# duptype id depth size device inode priority name\n";

  std::vector<Fileinfo>::iterator it;
  for (it = m_list.begin(); it != m_list.end(); ++it) {
    output << Fileinfo::getduptypestring(*it) << " " << it->getidentity() << " "
           << it->depth() << " " << it->size() << " " << it->device() << " "
           << it->inode() << " " << it->get_cmdline_index() << " " << it->name()
           << '\n';
  }
  output << "# end of file\n";
  f1.close();
  return 0;
}

// applies int f(duplicate,const original) on every duplicate.
// if f returns nonzero, something is wrong.
// returns how many times the function was invoked.
template<typename Function>
int
applyactiononfile(std::vector<Fileinfo>& m_list, Function f)
{

  std::vector<Fileinfo>::iterator it, src;
  src = m_list.end();

  int ntimesapplied = 0;

  // loop over files
  for (it = m_list.begin(); it != m_list.end(); ++it) {
    if (it->getduptype() == Fileinfo::DUPTYPE_FIRST_OCCURRENCE) {
      src = it;

      if (src->getidentity() <= 0) {
        std::cerr << "hmm. this file should have positive identity.\n";
      }
    } else if (it->getduptype() == Fileinfo::DUPTYPE_OUTSIDE_TREE ||
               it->getduptype() == Fileinfo::DUPTYPE_WITHIN_SAME_TREE) {
      // double check that "it" shall be ~linked to "src"
      if (it->getidentity() == -src->getidentity()) {
        // everything is in order. we may now ~link it to src.
        if (f(*it, *src)) {
          std::cerr << "Rdutil.cc: Failed to apply function f on it.\n";
        } else {
          ntimesapplied++;
        }
      } else
        std::cerr << "hmm. is list badly sorted?\n";
    }
  }

  return ntimesapplied;
}

// helper for dryruns
template<class Outputobject>
class dryrun_helper
{
public:
  dryrun_helper(Outputobject& out,
                std::string m1,
                std::string m2,
                std::string m3,
                int retval = 0)
    : m_m1(m1)
    , m_m2(m2)
    , m_m3(m3)
    , m_retval(retval)
    , m_out(out)
    , m_outputAname(true)
    , m_outputBname(true){};

  std::string m_m1, m_m2, m_m3;
  int m_retval;
  Outputobject& m_out;
  bool m_outputAname;
  bool m_outputBname;

  void disableAname(void) { m_outputAname = false; }
  void disableBname(void) { m_outputBname = false; }

  bool operator()(const Fileinfo& A, const Fileinfo& B)
  {
    std::string retstring = m_m1;
    if (m_outputAname)
      retstring += A.name();

    retstring += m_m2;

    if (m_outputBname)
      retstring += B.name();

    retstring += m_m3;

    m_out << retstring << '\n';

    return m_retval;
  }
};

int
Rdutil::deleteduplicates(bool dryrun) const
{
  if (dryrun) {
    dryrun_helper<std::ostream> obj(std::cout, "delete ", "", "");
    obj.disableBname();
    auto ret = applyactiononfile(m_list, obj);
    std::cout.flush();
    return ret;
  } else {
    return applyactiononfile(m_list, &Fileinfo::static_deletefile);
  }
}

int
Rdutil::makesymlinks(bool dryrun) const
{
  if (dryrun) {
    dryrun_helper<std::ostream> obj(std::cout, "symlink ", " to ", "");
    auto ret = applyactiononfile(m_list, obj);
    std::cout.flush();
    return ret;
  } else {
    return applyactiononfile(m_list, &Fileinfo::static_makesymlink);
  }
}

int
Rdutil::makehardlinks(bool dryrun) const
{
  if (dryrun) {
    dryrun_helper<std::ostream> obj(std::cout, "hardlink ", " to ", "");
    const auto ret = applyactiononfile(m_list, obj);
    std::cout.flush();
    return ret;
  } else
    return applyactiononfile(m_list, &Fileinfo::static_makehardlink);
}

// mark files with a unique number
void
Rdutil::markitems()
{
  std::int64_t fileno = 1;
  for (auto& file : m_list) {
    file.setidentity(fileno++);
  }
}

namespace {
bool
cmpDeviceInode(const Fileinfo& a, const Fileinfo& b)
{
  return std::make_tuple(a.device(), a.inode()) <
         std::make_tuple(b.device(), b.inode());
}
// compares rank as described in RANKING on man page.
bool
cmpRank(const Fileinfo& a, const Fileinfo& b)
{
  return std::make_tuple(a.get_cmdline_index(), a.depth(), a.getidentity()) <
         std::make_tuple(b.get_cmdline_index(), b.depth(), b.getidentity());
}

/**
 * goes through first to last, finds ranges of equal elements (determined by
 * cmp) and invokes callback on each subrange.
 * @param first
 * @param last
 * @param cmp
 * @param callback invoked as callback(subrangefirst,subrangelast)
 */
template<class Iterator, class Cmp, class Callback>
void
apply_on_range(Iterator first, Iterator last, Cmp cmp, Callback callback)
{
  assert(std::is_sorted(first, last, cmp));

  while (first != last) {
    auto p = std::equal_range(first, last, *first, cmp);
    // p.first will point to first. p.second will point to first+1 if no
    // duplicate is found
    assert(p.first == first);

    // a duplicate range with respect to cmp
    callback(p.first, p.second);

    // keep searching.
    first = p.second;
  }
}
} // anon. namespace
int
Rdutil::sortOnDeviceAndInode()
{

  std::sort(m_list.begin(), m_list.end(), cmpDeviceInode);
  return 0;
}

std::size_t
Rdutil::removeIdenticalInodes()
{
  // mark all elements as worth to keep
  for (auto& e : m_list) {
    e.setdeleteflag(false);
  }

  // sort list on device and inode.
  auto cmp = cmpDeviceInode;
  std::sort(m_list.begin(), m_list.end(), cmp);

  // loop over ranges of adjacent elements
  using Iterator = decltype(m_list.begin());
  apply_on_range(
    m_list.begin(), m_list.end(), cmp, [](Iterator first, Iterator last) {
      // let the highest-ranking element not be deleted. do this in order, to be
      // cache friendly.
      auto best = std::min_element(first, last, cmpRank);
      std::for_each(first, best, [](Fileinfo& f) { f.setdeleteflag(true); });
      best->setdeleteflag(false);
      std::for_each(best + 1, last, [](Fileinfo& f) { f.setdeleteflag(true); });
    });
  return cleanup();
}

int
Rdutil::sortlist(bool (*lessthan1)(const Fileinfo&, const Fileinfo&),
                 bool (*equal1)(const Fileinfo&, const Fileinfo&),
                 bool (*lessthan2)(const Fileinfo&, const Fileinfo&),
                 bool (*equal2)(const Fileinfo&, const Fileinfo&),
                 bool (*lessthan3)(const Fileinfo&, const Fileinfo&),
                 bool (*equal3)(const Fileinfo&, const Fileinfo&),
                 bool (*lessthan4)(const Fileinfo&, const Fileinfo&),
                 bool (*equal4)(const Fileinfo&, const Fileinfo&))
{
  MultiAttributeCompare<Fileinfo, 10> comp;
  comp.addattrib(lessthan1, equal1);
  comp.addattrib(lessthan2, equal2);
  comp.addattrib(lessthan3, equal3);
  comp.addattrib(lessthan4, equal4);

  sort(m_list.begin(), m_list.end(), comp);

  return 0;
}

// cleans up, by removing all items that have the deleteflag set to true.
std::size_t
Rdutil::cleanup()
{
  const auto size_before = m_list.size();
  auto it =
    std::remove_if(m_list.begin(), m_list.end(), Fileinfo::static_deleteflag);

  m_list.erase(it, m_list.end());

  const auto size_after = m_list.size();

  return size_before - size_after;
}

std::size_t
Rdutil::remove_small_files(Fileinfo::filesizetype minsize)
{
  const auto size_before = m_list.size();
  const auto begin = m_list.begin();
  const auto end = m_list.end();
  decltype(m_list.begin()) it;
  if (minsize == 0) {
    it = std::remove_if(begin, end, &Fileinfo::isempty);
  } else {
    it = std::remove_if(begin, end, [=](const Fileinfo& A) {
      return Fileinfo::is_smaller_than(A, minsize);
    });
  }
  m_list.erase(it, end);
  return size_before - m_list.size();
}

// total size
// opmode=0 just add everything
// opmode=1 only elements with m_duptype=Fileinfo::DUPTYPE_FIRST_OCCURRENCE
unsigned long long
Rdutil::totalsizeinbytes(int opmode) const
{
  // for some reason, for_each does not work.
  Rdutil::adder_helper adder;
  std::vector<Fileinfo>::iterator it;
  if (opmode == 0) {
    for (it = m_list.begin(); it != m_list.end(); ++it) {
      adder(*it);
    }
  } else if (opmode == 1) {
    for (it = m_list.begin(); it != m_list.end(); ++it) {
      if (it->getduptype() == Fileinfo::DUPTYPE_FIRST_OCCURRENCE) {
        adder(*it);
      }
    }
  } else {
    throw std::runtime_error("bad input, mode should be 0 or 1");
  }

  return adder.getsize();
}
namespace littlehelper {
// helper to make "size" into a more readable form.
int
calcrange(unsigned long long int& size)
{
  int range = 0;
  unsigned long long int tmp = 0ULL;
  while (size > 1024ULL) {
    tmp = size >> 9;
    size = (tmp >> 1);
    ++range;
  }

  // round up if necessary
  if (tmp & 0x1ULL)
    size++;

  return range;
}

// helper. source of capitalization rules etc:
// https://en.wikipedia.org/wiki/Binary_prefix
std::string
byteprefix(int range)
{
  switch (range) {
    case 0:
      return "B";
    case 1:
      return "KiB";
    case 2:
      return "MiB";
    case 3:
      return "GiB";
    case 4:
      return "TiB"; // Tebibyte
    case 5:
      return "PiB"; // Pebibyte
    case 6:
      return "EiB"; // Exbibyte
    default:
      return "!way too much!";
  }
}
}
std::ostream&
Rdutil::totalsize(std::ostream& out, int opmode) const
{
  auto size = totalsizeinbytes(opmode);
  const int range = littlehelper::calcrange(size);
  out << size << " " << littlehelper::byteprefix(range);
  return out;
}

std::ostream&
Rdutil::saveablespace(std::ostream& out) const
{
  unsigned long long int size = totalsizeinbytes(0) - totalsizeinbytes(1);
  int range = littlehelper::calcrange(size);
  out << size << " " << littlehelper::byteprefix(range);
  return out;
}

// marks non unique elements for deletion. list must be sorted first.
int
Rdutil::marknonuniq(bool (*equal1)(const Fileinfo&, const Fileinfo&),
                    bool (*equal2)(const Fileinfo&, const Fileinfo&),
                    bool (*equal3)(const Fileinfo&, const Fileinfo&),
                    bool (*equal4)(const Fileinfo&, const Fileinfo&))
{

  // create on object that can compare two files
  MultiAttributeCompare<Fileinfo, 10> comp;
  comp.addattrib(equal1);
  comp.addattrib(equal2);
  comp.addattrib(equal3);
  comp.addattrib(equal4);

  // an object to apply on duplicate regions
  ApplyOnDuplicateFunction<Fileinfo> apf;

  // set all delete flags to false
  std::vector<Fileinfo>::iterator it, start, stop;

  start = m_list.begin();
  stop = m_list.end();

  for (it = m_list.begin(); it != m_list.end(); ++it) {
    it->setdeleteflag(false);
  }

  apply_on_duplicate_regions<Fileinfo>(start, stop, comp, apf);

  return 0;
}

// marks uniq elements for deletion. list must be sorted first, before calling
// this.
int
Rdutil::markuniq(bool (*equal1)(const Fileinfo&, const Fileinfo&),
                 bool (*equal2)(const Fileinfo&, const Fileinfo&),
                 bool (*equal3)(const Fileinfo&, const Fileinfo&),
                 bool (*equal4)(const Fileinfo&, const Fileinfo&))
{
  // create on object that can compare two files
  MultiAttributeCompare<Fileinfo, 10> comp;
  comp.addattrib(equal1);
  comp.addattrib(equal2);
  comp.addattrib(equal3);
  comp.addattrib(equal4);

  // identify the regions with duplicates
  int a = 1;
  int ndup = 0;
  std::vector<Fileinfo>::iterator start, stop, segstart, segstop;
  start = m_list.begin();
  stop = m_list.end();

  std::vector<Fileinfo>::iterator it;

  for (it = start; it != stop; ++it) {
    it->setdeleteflag(true);
  }

  while (a > 0) {
    // find the first region
    a = find_duplicate_regions<Fileinfo, MultiAttributeCompare<Fileinfo, 10>>(
      start, stop, segstart, segstop, comp);

    if (a > 0) {
      // found region.
      // cout<<"found region with "<<a<<" duplicates."<<endl;

      // let the duplicate search start at a suitable place next time.
      start = segstop;

      // apply something on the objects that are duplicates
      for (it = segstart; it != segstop; ++it) {
        it->setdeleteflag(false);
      }
    }
    ndup += a;
  } // end while a>0

  return 0;
}

// marks duplicates
int
Rdutil::markduplicates(bool (*equal1)(const Fileinfo&, const Fileinfo&),
                       bool (*equal2)(const Fileinfo&, const Fileinfo&),
                       bool (*equal3)(const Fileinfo&, const Fileinfo&),
                       bool (*equal4)(const Fileinfo&, const Fileinfo&))
{
  // create on object that can compare two files
  MultiAttributeCompare<Fileinfo, 10> comp;
  comp.addattrib(equal1);
  comp.addattrib(equal2);
  comp.addattrib(equal3);
  comp.addattrib(equal4);

  // identify the regions with duplicates
  int a = 1;
  int ndup = 0;
  std::vector<Fileinfo>::iterator start, stop, segstart, segstop;
  start = m_list.begin();
  stop = m_list.end();
  while (a > 0) {
    // find the first region
    a = find_duplicate_regions<Fileinfo, MultiAttributeCompare<Fileinfo, 10>>(
      start, stop, segstart, segstop, comp);

    if (a > 0) {
      // found region
      // let the duplicate search start at a suitable place next time.
      start = segstop;

      // mark the file
      marksingle(segstart, segstop);
    }
    ndup += a;
  } // end while a>0

  return 0;
}

// formats output in a nice way.
int
Rdutil::marksingle(std::vector<Fileinfo>::iterator start,
                   std::vector<Fileinfo>::iterator stop)
{
  // sort on command line index - keep the other ordering (stable_sort instead
  // of sort)
  std::stable_sort(start, stop, &Fileinfo::compareoncmdlineindex);

  std::vector<Fileinfo>::iterator it;

  // only considering a file belonging to a different command line index as a
  // duplicate

  for (it = start; it != stop; ++it) {
    if (it == start) {
      it->setduptype(Fileinfo::DUPTYPE_FIRST_OCCURRENCE);
    } else {
      // point out the file that it is a copy of
      it->setidentity(-Fileinfo::identity(*start));
      if (it->get_cmdline_index() == start->get_cmdline_index() &&
          it != start) {
        it->setduptype(Fileinfo::DUPTYPE_WITHIN_SAME_TREE);
      } else {
        it->setduptype(Fileinfo::DUPTYPE_OUTSIDE_TREE);
      }
    }
  }

  return 0;
}

// read some bytes. note! destroys the order of the list.
int
Rdutil::fillwithbytes(enum Fileinfo::readtobuffermode type,
                      enum Fileinfo::readtobuffermode lasttype,
                      long nsecsleep)
{

  // first sort on inode (to read efficently from harddrive)
  sortlist(&Fileinfo::compareoninode, &Fileinfo::equalinode);

  std::vector<Fileinfo>::iterator it;
  if (nsecsleep <= 0)
    for (it = m_list.begin(); it != m_list.end(); ++it) {
      it->fillwithbytes(type, lasttype);
    }
  else {
    // shall we do sleep between each file or not
    struct timespec time;
    time.tv_sec = 0;
    time.tv_nsec = nsecsleep;

    for (it = m_list.begin(); it != m_list.end(); ++it) {
      it->fillwithbytes(type, lasttype);
      nanosleep(&time, 0);
    }
  }

  return 0;
}
