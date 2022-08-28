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
#include <cstring>
#include <getopt.h>
#include <iostream>
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

static void usage(void);
static const char *get_progname(const char *argv0);

static void print_dot_header(void);
static void print_dot_body(const Node *root);
static void print_dot_footer(void);

static bool parse_node(Node **root);
static string get_name(void);

int
main(int argc, char **argv)
{
	int c;
	int optidx;
	const char *shortopts = "hv";
	struct option longopts[] = {
		{"help",    no_argument, 0, 'h' },
		{"version", no_argument, 0, 'v' },
		{ NULL,     0,           0,  0  }
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
		default:
			fprintf(stderr, "Try \"%s --help\" for more information.\n", progname);
			exit(1);
		}
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

	bfs.push(root);

	while (!bfs.empty()) {
		const Node *parent = bfs.front();
		char dot_node[NODE_LEN] = { 0 };
		int max = sizeof(dot_node) - 1;
		int len = 0;

		len += snprintf(dot_node + len, max - len,
						"node_%lu [\n"
						"  label=<<table border=\"0\" cellspacing=\"0\">\n"
						"    <tr>"
						"      <td port=\"f0\" border=\"1\">"
						"        <B>%s</B>"
						"      </td>"
						"    </tr>\n",
						parent->suffix, parent->name.c_str());

		for (size_t i = 0; i < parent->elems.size(); i++) {
			const Node *child = parent->elems[i];

			if (!child->elems.empty()) {
				bfs.push(child);
			}

			len += snprintf(dot_node + len, max - len,
							"    <tr><td port=\"f%lu\" border=\"1\">%s</td></tr>\n",
							child->index, child->name.c_str());
		}

		len += snprintf(dot_node + len, max - len, "  </table>>\n];");

		if (parent->type != TYPE_LIST && parent->type != TYPE_HIDE) {
			cout <<dot_node <<endl;
		}

		for (size_t i = 0; i < parent->edges.size(); i++) {
			cout <<parent->edges[i] <<endl;
		}

		bfs.pop();
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
				if (top == NULL) {
					nodes_stack.push(node);
					cerr <<"STACK: node push " <<node->name <<" at stack "
						 <<nodes_stack.size() <<endl;
				} else {
					char edge[EDGE_LEN] = { 0 };

					if (prev_is_item) {
						Node *tmp = top;

						assert(!top->elems.empty());

						top = top->elems.back();
						top->type = TYPE_HIDE;
						top->suffix = tmp->suffix;
					}

					if (top->type == TYPE_LIST) {
						if (top->elems.empty()) {
							snprintf(edge, sizeof(edge), "node_%lu:f%lu -> node_%lu:f0",
									 top->suffix, top->index, node->suffix);
						} else {
							Node *prev = top->elems.back();
							snprintf(edge, sizeof(edge), "node_%lu:f0 -> node_%lu:f0",
									 prev->suffix, node->suffix);
						}
					} else {
						snprintf(edge, sizeof(edge), "node_%lu:f%lu -> node_%lu:f0",
								 top->suffix, top->index, node->suffix);
					}

					top->edges.push_back(edge);
					top->elems.push_back(node);
					node->index = top->elems.size();

					nodes_stack.push(node);
					cerr <<"STACK: node push " <<node->name <<" at stack "
						 <<nodes_stack.size() <<endl;
				}

				prev_is_item = false;
				break;
			}
		case '}':
			{
				assert(!nodes_stack.empty());

				top = nodes_stack.top();
				cerr <<"STACK: node pop " <<top->name << " from stack "
					 <<nodes_stack.size() <<endl;

				prev_is_item = false;

				nodes_stack.pop();

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

				cerr <<"STACK: list push "<< node->name << " at stack "
					 <<nodes_stack.size() <<endl;

				prev_is_item = false;
				break;
			}
		case ')':
			{
				assert(!nodes_stack.empty());

				top = nodes_stack.top();
				cerr <<"STACK: list pop " <<top->name <<" from stack "
					 << nodes_stack.size() <<endl;

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
