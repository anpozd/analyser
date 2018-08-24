
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <regex>
#include <vector>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/system/error_code.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>


namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace sys = boost::system;

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

static bool is_source_file(const fs::directory_entry &dir_entry)
{
    static const fs::path extensions[] = {".h", ".hpp", ".c", ".cpp"};

    if (!is_regular_file(dir_entry.status()))
        return false;

    const fs::path file_extension = dir_entry.path().extension();
    if (!file_extension.empty()) {
        for (const fs::path &extension : extensions) {
            if (file_extension.compare(extension) == 0)
                return true;
        }
    }

    return false;
}

static std::vector<fs::path> list_source_files(const fs::path &dir_path)
{
    std::vector<fs::path> source_files;

    sys::error_code error_code;
    fs::recursive_directory_iterator dir_iterator(dir_path, fs::symlink_option::recurse, error_code);
    fs::recursive_directory_iterator end_iterator;
    for (; dir_iterator != end_iterator; dir_iterator.increment(error_code)) {
        if (error_code) {
            std::cerr << error_code.message() << std::endl;
            continue;
        }

        if (is_source_file(*dir_iterator))
            source_files.push_back(dir_iterator->path());
    }

    return source_files;
}

struct include_directive {
    std::string pathname;
    bool is_global;
};

static std::vector<include_directive> grep_include_directives(const fs::path &file_path)
{
    std::vector<include_directive> include_directives;

    fs::ifstream source_file(file_path);
    if (!source_file) {
        std::cerr << "failed to open " << file_path << " for reading\n";
        return include_directives;
    }

    static const std::regex include_regex(
        "\\s*#\\s*include\\s*(?:(?:<(\\S+)>)|(?:\"(\\S+)\"))\\s*(.*)",
        std::regex::ECMAScript | std::regex::optimize);

   for (std::string line; std::getline(source_file, line);) {

       std::smatch match;
       if (std::regex_match(line, match, include_regex)) {
            if (match[1].matched)
                include_directives.push_back({match.str(1), true});
            else if (match[2].matched)
                include_directives.push_back({match.str(2), false});

            line = match.str(3);
            if (line.empty())
                continue;

            /* TODO add processing of multiline comments */
       }
    }

    return include_directives;
}

struct resolved_include_directive {
    include_directive directive;
    fs::path path;
};

static resolved_include_directive resolve_include_directive(
    const include_directive &directive, const fs::path &file_path,
    const std::vector<fs::path> &include_dirs) {

    fs::path resolved_path, include_path(directive.pathname);
    sys::error_code error_code;

    if (!include_path.is_absolute()) {
        switch (directive.is_global) {
        case false:
            resolved_path = fs::canonical(include_path, file_path.parent_path(), error_code);
            if (!error_code)
                break;

        case true:
            if (!include_dirs.empty()) {
                for (const fs::path &include_dir : include_dirs) {
                    resolved_path = fs::canonical(include_path, include_dir, error_code);
                    if (!error_code)
                        break;
                }
            } else {
                resolved_path = fs::path();
            }
        }
    } else {
        resolved_path = fs::canonical(include_path, error_code);
    }

    /* TODO warn if resolved_path isn't a regular file */

    return resolved_include_directive {
        directive,
        resolved_path
    };
}

using dependency_table = std::map<fs::path, std::vector<resolved_include_directive> >;

static dependency_table build_dependency_table(
    const std::vector<fs::path> &source_files, const std::vector<fs::path> &include_dirs)
{
    /* TODO try using unordered_map for both*/
    dependency_table dependencies;
    std::map<std::string, resolved_include_directive> global_inclusions;

    for (const auto &file : source_files) {
        for (const auto &directive : grep_include_directives(file)) {

            auto &file_dependencies = dependencies[file];
            auto inclusion_iter = global_inclusions.end();
            auto dependency_iter = file_dependencies.end();

            switch (directive.is_global) {
            case true:
                inclusion_iter = global_inclusions.find(directive.pathname);
                if (inclusion_iter != global_inclusions.end()) {
                    file_dependencies.push_back(inclusion_iter->second);
                    break; /* go out of switch since dependency is already resolved */
                }
                /* fall through to resolve global dependency */

            case false:
                dependency_iter = file_dependencies.insert(
                    file_dependencies.end(),
                    resolve_include_directive(directive, file, include_dirs));
                if (directive.is_global)
                    global_inclusions[directive.pathname] = *dependency_iter;
            }
        }
    }

    return dependencies;
}

static void print_file_dependency_tree(
    const fs::path &source_file, const dependency_table &dependencies,
    const fs::path &root_dir, unsigned int recursion_depth = 0,
    std::set<fs::path> visited_files = std::set<fs::path>())
{
    static const char *indentation_str = "....";
    static const char *brackets = "<>";
    static const char *quotation_marks = "\"\"";
    static const char *absent_mark = " ! ";
    static const char *present_mark = " ";
    static const char *loop_mark = " @ ";

    visited_files.insert(source_file);

    if (!recursion_depth)
        std::cout << source_file << std::endl; /* TODO make use of root_dir */

    auto dependencies_iter = dependencies.find(source_file);
    if (dependencies_iter != dependencies.end()) {
        for (const auto &resolved_inclusion : dependencies_iter->second) {
            for (unsigned int i = 0; i <= recursion_depth; ++i)
                std::cout << indentation_str;

            const bool inclusion_visited = visited_files.count(resolved_inclusion.path);
            const bool inclusion_absent = resolved_inclusion.path.empty();
            std::cout << (inclusion_visited ? loop_mark
                                            : (inclusion_absent ? absent_mark : present_mark));

            const char *chars = resolved_inclusion.directive.is_global ? brackets : quotation_marks;
            std::cout << chars[0] << resolved_inclusion.directive.pathname << chars[1];

            if (!inclusion_absent) {
                /* TODO make use of root_dir */
                std::cout << " -> " << resolved_inclusion.path << std::endl;
                if (!inclusion_visited) {
                    print_file_dependency_tree(
                        resolved_inclusion.path, dependencies, root_dir,
                        recursion_depth + 1, visited_files);
                }
            } else {
                std::cout << std::endl;
            }
        }
    }
}

static void print_dependency_tree(
    const std::vector<fs::path> &source_files, const dependency_table &dependencies,
    const fs::path &root_dir)
{
    for (const auto &file : source_files) {
        print_file_dependency_tree(file, dependencies, root_dir);
        std::cout << std::endl;
    }
}

int main(int argc, char *argv[])
{
    const command_line_arguments arguments = parse_command_line(argc, argv);

    try {
        const fs::path sources_dir = to_dir_path(arguments.sources_dir);
        const std::vector<fs::path> include_dirs(
            boost::make_transform_iterator(arguments.include_dirs.begin(), to_dir_path),
            boost::make_transform_iterator(arguments.include_dirs.end(), to_dir_path));

        const std::vector<fs::path> source_files = list_source_files(sources_dir);
        const dependency_table dependencies = build_dependency_table(source_files, include_dirs);

        print_dependency_tree(source_files, dependencies, sources_dir);

    } catch (const std::exception &exception) {
        std::cerr << exception.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::exit(EXIT_SUCCESS);
}
