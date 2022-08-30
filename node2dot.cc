/*----------------------------------------------------------------------------
 *
 * node2dot
 *     Convert a postgresql node tree into dot language.
 *
 * g++ node2dot.cc -o node2dot
 *
 *----------------------------------------------------------------------------
 */
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <queue>
#include <stack>
#include <string>
#include <vector>

using namespace std;

#define VERSION	"0.1"

#define EDGE_LEN	128
#define NODE_LEN    4096


enum
{
	TYPE_HIDE = 0,
	TYPE_NODE,
	TYPE_LIST,
	TYPE_ITEM
};

struct Node
{
	int    type;
	string name;
	size_t suffix;
	size_t index;
	vector<Node *> elems;
	vector<string> edges;
};

static const char *progname;
static bool enable_color = false;
static const char *color_map_file = NULL;
static map<string, string> node_color_map;

static void usage(void);
static const char *get_progname(const char *argv0);
static bool load_node_color_map(void);

static void print_dot_header(void);
static void print_dot_body(const Node *root);
static void print_dot_footer(void);

static bool parse_node(Node **root);
static string get_name(void);
static string get_node_header(size_t suffix, const string& name);
static string get_node_body(size_t suffix, const string& name);
static string get_node_footer(void);
static string get_node_edge(size_t src_suffix, size_t src_index,
							size_t dst_suffix, size_t dst_index,
							bool list);
static string get_node_border_color(const string& name);
static string get_node_color(const string& name);

int
main(int argc, char **argv)
{
	int c;
	int optidx;
	const char *shortopts = "hvcn:";
	struct option longopts[] = {
		{"help",             no_argument,       0, 'h' },
		{"version",          no_argument,       0, 'v' },
		{"color",            no_argument,       0, 'c' },
		{"node-color-map",   required_argument, 0, 'n' },
		{ NULL,              0,                 0,  0  }
	};

	progname = get_progname(argv[0]);

	while ((c = getopt_long(argc, argv, shortopts, longopts, &optidx)) != -1) {
		switch (c) {
		case 'h':
			usage();
			exit(0);
		case 'v':
			printf("%s %s\n", progname, VERSION);
			exit(0);
		case 'c':
			enable_color = true;
			break;
		case 'n':
			color_map_file = optarg;
			break;
		default:
			fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
			exit(1);
		}
	}

	if (!load_node_color_map()) {
		exit(1);
	}

	Node *node = NULL;

	if (!parse_node(&node)) {
		fprintf(stderr, "parse node tree failed\n");
		exit(1);
	}

	print_dot_header();
	print_dot_body(node);
	print_dot_footer();

	return 0;
}


static void
usage(void)
{
	printf("Convert postgresql node tree into dot language.\n");
	printf("\nUsage:\n");
	printf("  node2dot [OPTIONS]\n");
	printf("\nOptions:\n");
	printf("  -h, --help       show this page and exit\n");
	printf("  -v, --version    show version and exit\n");
	printf("  -c, --color      render the output with color\n");
	printf("  -n, --node-color-map=NODE_COLOR_MAP\n"
		   "                   specify the border color mapping file for nodes (with -c option)\n");
}

static const char *
get_progname(const char *argv0)
{
	const char *name = strrchr(argv0, '/');

	if (name) {
		name++;	/* skip dir sep */
	} else {
		name = argv0;
	}

	return name;
}

static void
print_dot_header(void)
{
	printf("digraph PGNodeGraph {\n");
	printf("node [shape=none];\n");
	printf("rankdir=LR;\n");
	printf("size=\"100000,100000\";\n");
}

static void
print_dot_body(const Node *root)
{
	queue<const Node *> bfs;

	/* Firstly, write the information of nodes. */
	bfs.push(root);
	while (!bfs.empty()) {
		string      nodeinfo;
		const Node *parent = bfs.front();

		bfs.pop();

		nodeinfo = get_node_header(parent->suffix, parent->name);
		for (size_t i = 0; i < parent->elems.size(); i++) {
			const Node *child = parent->elems[i];

			/*
			 * If this node has one or more children, we should output
			 * it as a separat node.
			 */
			if (!child->elems.empty()) {
				bfs.push(child);
			}

			nodeinfo += get_node_body(child->index, child->name);
		}
		nodeinfo += get_node_footer();

		if (parent->type != TYPE_LIST && parent->type != TYPE_HIDE) {
			cout <<nodeinfo <<endl;
		}
	}

	/* Then, write the edges between nodes. */
	bfs.push(root);
	while (!bfs.empty()) {
		const Node *curr = bfs.front();

		bfs.pop();
		for (size_t i = 0; i < curr->elems.size(); i++) {
			bfs.push(curr->elems[i]);
		}

		for (size_t i = 0; i < curr->edges.size(); i++) {
			cout <<curr->edges[i] <<endl;
		}

		/* Do not forget to release the memory. */
		delete curr;
	}
}

static void
print_dot_footer(void)
{
	printf("}\n");
}

static bool
parse_node(Node **root)
{
	int ch;
	size_t node_suffix = 0;
	Node *top;
	bool prev_is_item = false;
	stack<Node *> nodes_stack;

	*root = NULL;
	while ((ch = getchar()) != EOF) {
		switch (ch) {
		case '{': /* start a new struct node */
			{
				Node *node = new Node();

				node->type = TYPE_NODE;
				node->name = get_name();
				node->index = 0;
				node->suffix = node_suffix++;

				top = nodes_stack.empty() ? NULL : nodes_stack.top();
				if (top != NULL) {
					size_t src_suffix, src_index;
					size_t dst_suffix, dst_index;
					string edgeinfo;

					if (prev_is_item) {
						Node *tmp = top;

						assert(!top->elems.empty());

						top = top->elems.back();
						top->type = TYPE_HIDE;
						top->suffix = tmp->suffix;
					}

					src_suffix = top->suffix;
					src_index = top->index;
					dst_suffix = node->suffix;
					dst_index = 0;

					/*
					 * We should update the source information if it's
					 * a list type and it's elems is not empty.
					 */
					if (top->type == TYPE_LIST) {
						if (!top->elems.empty()) {
							Node *prev = top->elems.back();

							src_suffix = prev->suffix;
							src_index = 0;
						}
					}

					edgeinfo = get_node_edge(src_suffix, src_index,
											 dst_suffix, dst_index,
											 top->type == TYPE_LIST);

					top->edges.push_back(edgeinfo);
					top->elems.push_back(node);
					node->index = top->elems.size();
				}

				nodes_stack.push(node);

#ifdef DEBUG
				cerr <<"STACK: node push " <<node->name <<" at stack "
					 <<nodes_stack.size() <<endl;
#endif

				prev_is_item = false;
				break;
			}
		case '}':
			{
				assert(!nodes_stack.empty());

				top = nodes_stack.top();

#ifdef DEBUG
				cerr <<"STACK: node pop " <<top->name << " from stack "
					 <<nodes_stack.size() <<endl;
#endif

				nodes_stack.pop();
				prev_is_item = false;

				if (nodes_stack.empty()) {
					*root = top;
					return true;
				}

				break;
			}
		case '(':
			{
				Node *node;

				assert(!nodes_stack.empty());

				top = nodes_stack.top();

				assert(!top->elems.empty());

				node = top->elems.back();
				node->type = TYPE_LIST;
				node->suffix = top->suffix;

				nodes_stack.push(node);

#ifdef DEBUG
				cerr <<"STACK: list push "<< node->name << " at stack "
					 <<nodes_stack.size() <<endl;
#endif

				prev_is_item = false;
				break;
			}
		case ')':
			{
				assert(!nodes_stack.empty());

				top = nodes_stack.top();

#ifdef DEBUG
				cerr <<"STACK: list pop " <<top->name <<" from stack "
					 << nodes_stack.size() <<endl;
#endif

				nodes_stack.pop();
				prev_is_item = false;

				break;
			}
		case ':':
			{
				Node *node = new Node();

				assert(!nodes_stack.empty());

				node->type = TYPE_ITEM;
				node->name = get_name();
				node->suffix = node_suffix++;

				/* Get top node and push current node in its elems. */
				top = nodes_stack.top();
				top->elems.push_back(node);
				node->index = top->elems.size();

				prev_is_item = true;

				break;
			}
		default:
			{
				/* ignore */
				break;
			}
		}
	}

	return nodes_stack.empty();
}

static string
get_name(void)
{
	int ch;
	string name;

	while ((ch = getchar())) {
		if (ch == ':' || ch == '{' || ch == '}') {
			break;
		} else if (ch == '(') {
			/*
			 * If this is a list, try to get next non space character
			 * to determine what to do.
			 */
			char tmp = getchar();
			while (isspace(tmp)) {
				tmp = getchar();
			}

			if (tmp == '{') {
				ungetc(tmp, stdin);
				break;
			}
			ungetc(tmp, stdin);
		}

		name.push_back(ch);

	}

	ungetc(ch, stdin);

	/* trim leading and trailing spaces */
	size_t endpos = name.find_last_not_of(" \t\r\n");
	size_t begpos = name.find_first_not_of(" \t\r\n");
	if (endpos != string::npos) {
		name = name.substr(0, endpos + 1);
		name = name.substr(begpos);
	}

	/* remove any illeagal character of dot language */
	for (size_t i = 0; i < name.size(); i++) {
		if (name[i] == '"') {
			name[i] = ' ';
		} else if (name[i] == '<' || name[i] == '>') {
			name[i] = '-';
		}
	}

	return name;
}

static string
get_node_header(size_t suffix, const string& name)
{
	char color[64] = { 0 };
	char node[NODE_LEN] = { 0 };
	string border_color("");

	if (enable_color) {
		border_color = get_node_border_color(name);

		/* first field background color */
		snprintf(color, sizeof(color), "%s",
				 get_node_color(name).c_str());
	}

	snprintf(node, sizeof(node),
			 "node_%lu [%s\n"
			 "  label=<<table border=\"0\" cellspacing=\"0\">\n"
			 "    <tr>\n"
			 "      <td port=\"f0\" border=\"1\" %s>\n"
			 "        <B>%s</B>\n"
			 "      </td>\n"
			 "    </tr>\n",
			 suffix, border_color.c_str(), color, name.c_str());

	return string(node);
}

static string
get_node_body(size_t suffix, const string& name)
{
	char node[NODE_LEN] = { 0 };

	snprintf(node, sizeof(node),
			 "    <tr><td port=\"f%lu\" border=\"1\">%s</td></tr>\n",
			 suffix, name.c_str());

	return string(node);
}

static string
get_node_footer(void)
{
	return string("  </table>>\n];");
}

static string
get_node_edge(size_t src_suffix, size_t src_index,
			  size_t dst_suffix, size_t dst_index,
			  bool list)
{
	char color[64] = { 0 };
	char edge[EDGE_LEN] = { 0 };

	if (enable_color) {
		if (list) {
			snprintf(color, sizeof(color), "[color=blue]");
		} else {
			snprintf(color, sizeof(color), "[color=green]");
		}
	}

	snprintf(edge, sizeof(edge),
			 "node_%lu:f%lu -> node_%lu:f%lu %s;",
			 src_suffix, src_index, dst_suffix, dst_index, color);

	return string(edge);
}

static string
get_node_border_color(const string& name)
{
	string color("color=black");

	if (!node_color_map.empty()) {
		map<string, string>::iterator it = node_color_map.find(name);
		if (it != node_color_map.end()) {
			return "color=" + it->second;
		}
	} else {
		/*
		 * Add more default color for nodes after here. For more color name, see:
		 * https://graphviz.org/doc/info/colors.html
		 */
		if (name.compare("QUERY") == 0) {
			color = "color=skyblue";
		} else if (name.compare("PLANNEDSTMT") == 0) {
			color = "color=pink";
		} else if (name.compare("TARGETENTRY") == 0) {
			color = "color=sienna";
		}
	}

	return color;
}

static string
get_node_color(const string& name)
{
	map<string, string>::iterator it = node_color_map.find(name);

	if (it != node_color_map.end()) {
		return "bgcolor=\"" + it->second + "\"";
	}

	return string("");
}

static bool
load_node_color_map(void)
{
	ifstream infile;

	if (color_map_file == NULL)
		return true;

	try {
		string node_name, node_color;

		infile.open(color_map_file);
		if (infile.fail())
			throw string(color_map_file);

		node_color_map.clear();
		while (infile >> node_name >> node_color) {
			node_color_map.insert(pair<string, string>(node_name, node_color));
		}

		infile.close();
	} catch (string e) {
		fprintf(stderr, "%s: could not open file \"%s\" for reading\n",
				progname, e.c_str());
		return false;
	}

#ifdef DEBUG
	for (map<string, string>::iterator it = node_color_map.begin();
		 it != node_color_map.end(); ++it) {
		cerr <<it->first << " = " <<it->second <<endl;
	}
#endif

	return true;
}
