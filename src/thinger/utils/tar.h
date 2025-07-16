#pragma once

#include <filesystem>
#include <vector>

#include <archive.h>
#include <archive_entry.h>

#include <pwd.h>
#include <grp.h>

namespace fs = std::filesystem;

namespace utils::tar {

  class error : public std::exception {
  public:
    explicit error(struct archive* a) :
        errno_(archive_errno(a)),
        error_string_(archive_error_string(a))
    {}

    [[nodiscard]]const char* what() const noexcept override {
      return error_string_.c_str();
    }

  private:
    int errno_;
    std::string error_string_;
  };

}

namespace utils::tar::write {

  struct archive* create_archive(const std::string_view& dest_file) {

    struct archive *a;

    // Creating archive
    a = archive_write_new();

    // Set compression
    if (dest_file.ends_with("tgz") || dest_file.ends_with("gz")) {
      archive_write_add_filter_gzip(a);
    }

    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, dest_file.data());

    return a;

  }

  // Adds a single file to an archive
  ssize_t add_entry(archive* a, const std::string_view& filename) {

    struct archive_entry *entry;

    struct stat st{};
    char buff[8192];
    int fd;
    ssize_t len;
    ssize_t r;

    // Clear the entry
    //archive_entry_clear(entry);

    stat(filename.data(), &st);
    entry = archive_entry_new();
    archive_entry_set_pathname_utf8(entry, filename.data());
    archive_entry_copy_stat(entry, &st); // copies all file attributes

    // Add entry to the archive
    archive_write_header(a, entry);

    fd = open(filename.data(), O_RDONLY);
    len = read(fd, buff, sizeof(buff));
    while ( len > 0 ) {
      r = archive_write_data(a, buff, len);
      if ( r < 0 )
        return r;
      len = read(fd, buff, sizeof(buff));
    }

    archive_entry_free(entry);
    ::close(fd);

    return ARCHIVE_OK;

  }

  // Adds a directory to an existing archive
  ssize_t add_directory(archive* a, const std::string_view& directory_path) {

    // Add file or recursive directory file given a full path
    std::vector<std::string> filenames;
    if ( fs::is_directory(directory_path) ) {
      for(auto& p: fs::recursive_directory_iterator(directory_path)) {
        filenames.push_back(p.path()); // add directories and regular files
      }
    }

    for (const std::string& filename : filenames) {

      ssize_t r = add_entry(a, filename);

      if ( r < 0 ) return r;

    }

    return ARCHIVE_OK;

  }

  void close_archive(struct archive* a) {
    int r;
    r = archive_write_close(a);
    if ( r != ARCHIVE_OK )
      throw error( a );
    r = archive_write_free(a);
    if ( r != ARCHIVE_OK )
      throw error( a );
  }

}

namespace utils::tar::read {

  struct archive* create_archive(std::string_view filename) {

    struct archive *a;
    int r;

    // Creating archive
    a = archive_read_new();

    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    r = archive_read_open_filename(a, filename.data(), 10240);
    if ( r != ARCHIVE_OK )
      throw error( a );

    return a;

  }

  void close_archive(archive* a) {
    int r;
    r = archive_read_close(a);
    if ( r != ARCHIVE_OK )
      throw error( a );
    r = archive_read_free(a);
    if ( r != ARCHIVE_OK )
      throw error( a );
  }

  std::vector<std::string> list_files(archive* a) {

    struct archive_entry *entry;

    std::vector<std::string> files;

    while ( archive_read_next_header(a, &entry) == ARCHIVE_OK )
      files.emplace_back( archive_entry_pathname(entry) );

    return files;

  }

  // Returns the number of bytes written
  bool copy_data(struct archive *ar, struct archive *aw) {
    la_ssize_t  r;
    const void *buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
      r = archive_read_data_block(ar, &buff, &size, &offset);
      if (r == ARCHIVE_EOF)
        return true;
      if (r < ARCHIVE_OK)
        throw error( ar );
      r = archive_write_data_block(aw, buff, size, offset);
      if (r < ARCHIVE_OK) {
        throw error( aw );
      }
    }
  }

  bool extract_one(struct archive* a, struct archive* ext, struct archive_entry* entry) {

    int flags;
    la_ssize_t r;

    /* Select which attributes we want to restore. */
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;
    flags |= ARCHIVE_EXTRACT_OWNER;

    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    // Extract only the file we need
    r = archive_write_header(ext, entry);
    if (r < ARCHIVE_OK) {
      throw error( ext );
    }
    else if (archive_entry_size(entry) > 0) {
      r = copy_data(a, ext);
      if (r < ARCHIVE_OK) {
        throw error( a );
      }
    }

    r = archive_write_finish_entry(ext);
    if (r < ARCHIVE_OK) {
      if ( r == ARCHIVE_WARN )
        LOG_WARNING(archive_error_string( ext ));
      else {
        throw error(ext);
      }
    }

    return true;

  }

  // Extracts only the indicated file
  bool extract_file(struct archive* a, std::string_view file) {

    struct archive *ext;
    struct archive_entry *entry;

    // Open in disk
    ext = archive_write_disk_new();

    while ( archive_read_next_header(a, &entry) == ARCHIVE_OK ) {

      if ( std::string_view( archive_entry_pathname(entry) ) == file )
        extract_one(a, ext, entry);

    }

    write::close_archive( ext );

    return true;

  }

  bool extract(struct archive *a) {

    struct archive *ext;
    struct archive_entry *entry;

    // Open in disk
    ext = archive_write_disk_new();

    while ( archive_read_next_header(a, &entry) == ARCHIVE_OK )
      extract_one(a, ext, entry);

    write::close_archive( ext );

    return true;

  }

}