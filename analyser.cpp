
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <boost/program_options.hpp>


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

int main(int argc, char *argv[])
{
    const command_line_arguments arguments = parse_command_line(argc, argv);

    std::cout << "source dir: " << arguments.sources_dir << std::endl;
    std::cout << "include dirs: ";
    for (const std::string& dir : arguments.include_dirs)
        std::cout << dir << ' ';
    std::cout << std::endl;

    std::exit(EXIT_SUCCESS);
}
