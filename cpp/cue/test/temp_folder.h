// temp_folder.h: a folder of its own for one test, removed afterwards (ICS-037).
#ifndef ICS_CUE_TEST_TEMP_FOLDER_H
#define ICS_CUE_TEST_TEMP_FOLDER_H

#include <filesystem>
#include <string>
#include <system_error>

namespace ics::cue_test
{

// An empty folder under the system's temporary folder, named for the test.
class Temp_folder
{
public:
  // Makes the folder, emptied of anything an earlier run left.
  explicit Temp_folder(const std::string& name)
    : path_(std::filesystem::temp_directory_path() / ("ics-cue-" + name))
  {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directories(path_, ignored);
  }

  // Removes the folder and everything in it.
  ~Temp_folder()
  {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  Temp_folder(const Temp_folder&) = delete;
  Temp_folder& operator=(const Temp_folder&) = delete;
  Temp_folder(Temp_folder&&) = delete;
  Temp_folder& operator=(Temp_folder&&) = delete;

  // The folder.
  [[nodiscard]] const std::filesystem::path& path() const
  {
    return path_;
  }

private:
  // The folder.
  std::filesystem::path path_;
};

}  // namespace ics::cue_test

#endif  // ICS_CUE_TEST_TEMP_FOLDER_H
