/*
   copyright 20016-2017 Paul Dreik (earlier Paul Sundvall)
   Distributed under GPL v 2.0 or later, at your option.
   See LICENSE for further details.

   this file contains functions and templates that implement most of the
   functionality in rdfind.
 */
#ifndef rdutil_hh
#define rdutil_hh

#include "Fileinfo.hh" //file container
#include <vector>

class Rdutil
{
public:
  explicit Rdutil(std::vector<Fileinfo>& list)
    : m_list(list){};

  // print file names to a file, with extra information.
  int printtofile(const std::string& filename) const;

  // mark files with a unique number
  void markitems();

  /**
   * sorts the list on device and inode. not guaranteed to be stable.
   * @return
   */
  int sortOnDeviceAndInode();

  /**
   * for each group of identical inodes, only keep the one with the highest
   * rank.
   * @return number of elements removed
   */
  std::size_t removeIdenticalInodes();

  // sort list on multiple attributes.
  int sortlist(bool (*lessthan1)(const Fileinfo&, const Fileinfo&),
               bool (*equal1)(const Fileinfo&, const Fileinfo&),
               bool (*lessthan2)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*equal2)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*lessthan3)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*equal3)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*lessthan4)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*equal4)(const Fileinfo&, const Fileinfo&) = NULL);

  /// removes all items from the list, that have the deleteflag set to true.
  std::size_t cleanup();

  // marks non unique elements for deletion. list must be sorted first.
  // this is good to eliminate duplicates on inode, to prevent from
  // reading hardlinked files, or repeated input arguments to the main program.
  int marknonuniq(bool (*equal1)(const Fileinfo&, const Fileinfo&),
                  bool (*equal2)(const Fileinfo&, const Fileinfo&) = NULL,
                  bool (*equal3)(const Fileinfo&, const Fileinfo&) = NULL,
                  bool (*equal4)(const Fileinfo&, const Fileinfo&) = NULL);

  // marks uniq elements for deletion (remember, this is a duplicate finder!)
  // list must be sorted first, before calling this.
  int markuniq(bool (*equal1)(const Fileinfo&, const Fileinfo&),
               bool (*equal2)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*equal3)(const Fileinfo&, const Fileinfo&) = NULL,
               bool (*equal4)(const Fileinfo&, const Fileinfo&) = NULL);

  // marks duplicates with tags, depending on their nature.
  // shall be used when everything is done, and sorted.
  int markduplicates(bool (*equal1)(const Fileinfo&, const Fileinfo&),
                     bool (*equal2)(const Fileinfo&, const Fileinfo&) = NULL,
                     bool (*equal3)(const Fileinfo&, const Fileinfo&) = NULL,
                     bool (*equal4)(const Fileinfo&, const Fileinfo&) = NULL);

  // subfunction to above
  int marksingle(std::vector<Fileinfo>::iterator start,
                 std::vector<Fileinfo>::iterator stop);

  /**
   * Removes items with file size less than minsize
   * @return the number of removed elements.
   */
  std::size_t remove_small_files(Fileinfo::filesizetype minsize);

  // read some bytes. note! destroys the order of the list.
  // if lasttype is supplied, it does not reread files if they are shorter
  // than the file length. (unnecessary!). if -1, feature is turned off.
  // and file is read anyway.
  // if there is trouble with too much disk reading, sleeping for nsecsleep
  // nanoseconds can be made between each file.
  int fillwithbytes(enum Fileinfo::readtobuffermode type,
                    enum Fileinfo::readtobuffermode lasttype =
                      Fileinfo::readtobuffermode::NOT_DEFINED,
                    long nsecsleep = 0);

  // make symlinks of duplicates.
  int makesymlinks(bool dryrun) const;

  // make hardlinks of duplicates.
  int makehardlinks(bool dryrun) const;

  // delete duplicates.
  int deleteduplicates(bool dryrun) const;

  // a little helper class
  class adder_helper
  {
  public:
    adder_helper()
      : m_sum(0){};
    typedef unsigned long long int sizetype_t;
    sizetype_t m_sum;
    void operator()(const Fileinfo& A)
    {
      m_sum += static_cast<sizetype_t>(A.size());
    }
    sizetype_t getsize(void) const { return m_sum; }
  };

  // gets the total size, in bytes.
  // opmode=0 just add everything
  // opmode=1 only elements with m_duptype=Fileinfo::DUPTYPE_FIRST_OCCURRENCE
  unsigned long long int totalsizeinbytes(int opmode = 0) const;

  // outputs a nicely formatted string "45 bytes" or "3 Gibytes"
  // where 1024 is used as base
  std::ostream& totalsize(std::ostream& out, int opmode = 0) const;

  // outputs the saveable amount of space
  std::ostream& saveablespace(std::ostream& out) const;

private:
  std::vector<Fileinfo>& m_list;
};

#endif
