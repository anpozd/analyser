
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>


namespace fs = boost::filesystem;
namespace po = boost::program_options;

struct command_line_arguments {
    const std::string sources_dir;
    const std::vector<std::string> include_dirs;
};

static command_line_arguments parse_command_line(int argc, char *argv[])
{
    po::options_description visible_options("Allowed options");
    visible_options.add_options()
        ("help,h", "display this help and exit")
        ("include-dir,I",
         po::value<std::vector<std::string> >()->value_name("<dir>")->default_value(
            std::vector<std::string>(0), std::string()),
         "add the directory to the header files' search paths");

    po::options_description hidden_options;
    hidden_options.add_options()
        ("sources-dir,d",
         po::value<std::string>()->value_name("<dir>"),
         "specify the directory with the source code");

    po::options_description options;
    options.add(visible_options).add(hidden_options);

    po::positional_options_description positional;
    positional.add("sources-dir", 1);

    po::variables_map variables_map;
    try {
        po::store(
            po::command_line_parser(argc, argv).options(options).positional(positional).run(),
            variables_map);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }
    notify(variables_map);

    if (variables_map.count("help")) {
        std::cout << "Usage: analyser [OPTION]... DIRECTORY\n\n" << visible_options << std::endl;
        std::exit(EXIT_SUCCESS);
    }

    if (!variables_map.count("sources-dir")) {
        std::cerr << "directory with the source code haven't been specified on the command line\n";
        std::exit(EXIT_FAILURE);
    }

    return command_line_arguments {
        variables_map["sources-dir"].as<std::string>(),
        variables_map["include-dir"].as<std::vector<std::string>>()
    };
}

static fs::path to_resolved_path(const std::string &pathname)
{
    const fs::path path(pathname);
    return path.is_absolute() ? path : fs::canonical(path, fs::current_path());
}

static fs::path to_dir_path(const std::string &pathname)
{
    const fs::path path = to_resolved_path(pathname);
    fs::file_status status = fs::status(path);
    if (!fs::exists(status))
        throw std::invalid_argument(pathname + " doesn't exist");
    if (!fs::is_directory(status))
        throw std::invalid_argument(pathname + " isn't a directory");

    return path;
}

int main(int argc, char *argv[])
{
    const command_line_arguments arguments = parse_command_line(argc, argv);

    try {
        const fs::path sources_dir = to_dir_path(arguments.sources_dir);
        const std::vector<fs::path> include_dirs(
            boost::make_transform_iterator(arguments.include_dirs.begin(), to_dir_path),
            boost::make_transform_iterator(arguments.include_dirs.end(), to_dir_path));

        std::cout << "source dir: " << sources_dir << std::endl;
        std::cout << "include dirs: " << std::endl;
        for (const fs::path &dir : include_dirs)
            std::cout << dir << std::endl;

    } catch (const std::exception &exception) {
        std::cerr << exception.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::exit(EXIT_SUCCESS);
}
