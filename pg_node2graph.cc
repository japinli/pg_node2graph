/*----------------------------------------------------------------------------
 *
 * node2graph.cc
 *     Convert a PostgreSQL node tree into a picture.
 *
 * Copyright (c) 2022, Japin Li <japinli@hotmail.com>.
 *
 *----------------------------------------------------------------------------
 */
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <map>
#include <queue>
#include <stack>
#include <string>
#include <vector>

using namespace std;


/* Define pg_node2graph version string. */
#define VERSION    "0.2"


typedef struct node_color_s
{
    string bgcolor;
	string fontcolor;
} node_color_t;

typedef struct dot_color_map_s
{
	const char *name;
	node_color_t colors;
} dot_color_map_t;

typedef enum tag_e
{
	TagHide = 0,
	TagNode,
	TagList,
	TagItem
} tag_t;

typedef struct node_s node_t;

struct node_s
{
	tag_t            tag;
	string           name;
	size_t           index;		/* index in elems */
	size_t           suffix;	/* dot node suffix */
	vector<string>   edges;
	vector<node_t *> elems;
};


/* global variables */
static const char *progname;

static bool enable_color = false;
static bool enable_skip_empty = false;
static bool remove_dot_files = false;
static const char *color_map_filename = NULL;
static const char *picture_format = NULL;
static const char *img_directory = NULL;
static const char *dot_directory = NULL;

static map<string, node_color_t> node_color_mapping;

static dot_color_map_t default_node_color_mapping[] = {
	{ "QUERY",          { "skyblue",   "" } },
	{ "PLANNEDSTMT",    { "pink",      "" } },
	{ "TARGETENTRY",    { "sienna",    "" } },
	{ NULL,             { "",          "" } }
};


/* private functions declaration */
static const char *get_progname(const char *argv0);
static void usage(void);
static void version(void);
static void write_stderr(const char *fmt, ...);

static bool load_color_map(void);
static void load_default_color_map(void);
static vector<string> split_node_colors(const string& str);

static string ltrim(const string& str);
static string rtrim(const string& str);
static string trim(const string& str);

static bool check_dot_program(void);

static bool node2graph(const char *filename);
static node_t *parse_pg_node_tree(FILE *fp);
static string get_pg_node_name(FILE *fp);

static string get_dot_edge(size_t src_suffix, size_t src_index,
						   size_t dst_suffix, size_t dst_index, bool list);
static void write_dot_script(node_t *root, FILE *fp);
static string get_dot_node_header(size_t suffix, const string& name);
static string get_dot_node_body(size_t suffix, const string& name);
static string get_dot_node_footer(void);
static bool name_contains_empty(const string& name);

static string get_dot_filename(const string& pathname);
static string get_img_filename(const string& pathname);

static string format_colnames(const string& name);


int
main(int argc, char **argv)
{
	int c;
	const char *shortopts = "hvcD:I:n:rsT:";
	struct option longopts[] = {
		{ "help",           no_argument,        0, 'h' },
		{ "version",        no_argument,        0, 'v' },
		{ "color",          no_argument,        0, 'c' },
		{ "dot-directory",  required_argument,  0, 'D' },
		{ "img-directory",  required_argument,  0, 'I' },
		{ "node-color-map", required_argument,  0, 'n' },
		{ "remove-dots",    no_argument,        0, 'r' },
		{ "skip-empty",     no_argument,        0, 's' },
		{ NULL,             required_argument,  0, 'T' },
		{ NULL,             0,                  0,  0  }
	};

	progname = get_progname(argv[0]);

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage();
			exit(0);
		case 'v':
			version();
			exit(0);
		case 'c':
			enable_color = true;
			break;
		case 'D':
			dot_directory = optarg;
			break;
		case 'I':
			img_directory = optarg;
			break;
		case 'n':
			color_map_filename = optarg;
			break;
		case 'r':
			remove_dot_files = true;
			break;
		case 's':
			enable_skip_empty = true;
			break;
		case 'T':
			picture_format = optarg;
			break;
		default:
			write_stderr("Try \"%s --help\" for more information.\n", progname);
			exit(1);
		}
	}

	/* If we don't specify picture format, use png as default. */
	if (picture_format == NULL) {
		picture_format = "png";
	}

	if (!load_color_map()) {
		exit(1);
	}

	/* check dot program */
	if (!check_dot_program()) {
		exit(1);
	}

	for (int i = optind; i < argc; i++) {

		printf("processing \"%s\" ... ", argv[i]);
		fflush(stdout);
		if (node2graph(argv[i])) {
			printf("ok\n");
		} else {
			printf("failed\n");
		}
	}

	return 0;
}


static const char *
get_progname(const char *argv0)
{
	const char *basename = strrchr(argv0, '/');

	if (basename) {
		basename++;    /* skip directory separator */
	} else {
		basename = argv0;
	}

	return basename;
}

static void
usage(void)
{
	printf("Convert PostgreSQL node tree into picture.\n");
	printf("\nUsage:\n");
	printf("  %s [OPTIONS] <filename>...\n", progname);
	printf("\nOptions:\n");
	printf("  -h, --help           show this page and exit\n");
	printf("  -v, --version        show version and exit\n");
	printf("  -c, --color          render the output with color\n");
	printf("  -D, --dot-directory  specify temporary dot files directory\n");
	printf("  -I, --img-dorectory  specify output pictures directory\n");
	printf("  -n, --node-color-map=NODE_COLOR_MAP\n"
		   "                       specify the color mapping file (with -c option)\n");
	printf("  -r, --remove-dots    remove temporary dot files\n");
	printf("  -s, --skip-empty     skip empty fields\n");
	printf("  -T FORMAT            specify the format for the picture (default: png)\n");
	printf("\nReport bugs to <japinli@hotmail.com>\n");
}

static void
version(void)
{
	printf("%s %s\n", progname, VERSION);
}

static void
write_stderr(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/*
 * Try to load color map from config file.
 *
 * If disabled color or do not specify the color map file, return true
 * immediately.
 */
static bool
load_color_map(void)
{
	int lineno = 0;
	FILE *infile;
	char *buf = NULL;
	size_t len = 0;
	ssize_t nread;

	if (!enable_color) {
		return true;
	}

	if (color_map_filename == NULL) {
		load_default_color_map();
		return true;
	}

	infile = fopen(color_map_filename, "r");
	if (infile == NULL) {
		write_stderr("%s: could not open file \"%s\" for reading: %m\n",
					 progname, color_map_filename);
		return false;
	}

	while ((nread = getline(&buf, &len, infile)) != -1) {
		string line = trim(buf);
		vector<string> node_colors;

		lineno++;

		/* skip empty or comments line */
		if (line.empty() || line[0] == '#') {
			continue;
		}

		node_colors = split_node_colors(line);
		if (node_colors.size() < 2 || node_colors.size() > 3) {
			write_stderr("%s: invalid node colors mapping at line %d\n",
						 progname, lineno);
			continue;
		}

		node_color_t colors;

		colors.bgcolor = node_colors[1];
		colors.fontcolor = node_colors.size() == 3 ? node_colors[2] : "";

		node_color_mapping[node_colors[0]] = colors;
	}

	free(buf);

	if (fclose(infile) != 0) {
		write_stderr("%s: could not close file \"%s\": %m",
					 progname, color_map_filename);
		return false;
	}

	return true;
}

static void
load_default_color_map(void)
{
	dot_color_map_t *it;
	node_color_mapping.clear();

	/* load default color mapping */
	for (it = default_node_color_mapping; it->name != NULL; it++) {
		node_color_mapping[it->name] = it->colors;
	}
}

static vector<string>
split_node_colors(const string& str)
{
	vector<string> ret;
	string s = trim(str) + ",";	/* add a token for parse */
	size_t pos = s.find(',');
	size_t beg = 0;

	while (pos != string::npos) {
		ret.push_back(trim(s.substr(beg, pos - beg)));

		/* move to the next */
		beg = pos + 1;
		pos = s.find(',', beg);
	}

	return ret;
}

static string
ltrim(const string& str)
{
	size_t i = 0;

	while (isspace(str[i])) {
		i++;
	}

	return str.substr(i);
}

static string
rtrim(const string& str)
{
	size_t len = str.size();

	while (isspace(str[len - 1])) {
		len--;
	}

	return str.substr(0, len);
}

static string
trim(const string& str)
{
	return ltrim(rtrim(str));
}

/*
 * Check if the dot program exist or not.
 */
static bool
check_dot_program(void)
{
	char  retbuf[64] = { 0 };
	FILE *pipe;

	/* The `dot` program prints the version on stderr. */
	pipe = popen("dot -V 2>&1", "r");
	if (pipe == NULL) {
		write_stderr("%s: could not find \"dot\" program: %m", progname);
		return false;
	}

	fgets(retbuf, sizeof(retbuf), pipe);
	if (pclose(pipe) != 0) {
		write_stderr("%s: could not close pipe for \"dot -V\": %m", progname);
		return false;
	}

#ifdef DEBUG
	cerr <<retbuf;
#endif

	/* Make sure the `dot` program comes from Graphviz. */
	if (strstr(retbuf, "graphviz") == NULL) {
		write_stderr("%s: program \"dot\" doesn't come from Graphviz\n",
					 progname);
		return false;
	}

	return true;
}

static bool
node2graph(const char *filename)
{
	FILE *infp = NULL;
	FILE *dotfp = NULL;
	string dotfile = get_dot_filename(filename);
	string imgfile = get_img_filename(filename);
	string dotcmd;
	node_t *root;

	infp = fopen(filename, "r");
	if (infp == NULL) {
		write_stderr("%s: could not open file \"%s\" for reading: %m\n",
					 progname, filename);
		goto failed;
	}

	dotfp = fopen(dotfile.c_str(), "w");
	if (dotfp == NULL) {
		write_stderr("%s: could not open file \"%s\" for writing: %m\n",
					 progname, dotfile.c_str());
		goto failed;
	}

	root = parse_pg_node_tree(infp);
	if (root == NULL) {
		write_stderr("%s: could no parse node tree from file \"%s\"\n",
					 progname, filename);
		goto failed;
	}

	write_dot_script(root, dotfp);

	/* convert dot to image */
	dotcmd = "dot -T " + string(picture_format);
	dotcmd += " -o " + imgfile + " " + dotfile;

	if (system(dotcmd.c_str()) != 0) {
		write_stderr("%s: could not execute command \"%s\"\n",
					 progname, dotcmd.c_str());
		goto failed;
	}

 failed:

	if (remove_dot_files) {
		unlink(dotfile.c_str());
	}

	if (infp != NULL) {
		fclose(infp);
	}
	if (dotfp != NULL) {
		fclose(dotfp);
	}

	return true;
}

static node_t *
parse_pg_node_tree(FILE *fp)
{
	int ch;
	size_t node_suffix = 0;
	node_t *top;
	bool prev_is_item = false;
	stack<node_t *> nodes_stack;

	while ((ch = getc(fp)) != EOF) {
		switch (ch) {
		case '{':
			{
				node_t *node = new node_t();

				node->tag = TagNode;
				node->name = get_pg_node_name(fp);
				node->index = 0;
				node->suffix = node_suffix++;

				top = nodes_stack.empty() ? NULL : nodes_stack.top();
				if (top != NULL) {
					size_t src_suffix, src_index;
					size_t dst_suffix, dst_index;
					string edgeinfo;

					if (prev_is_item) {
						node_t *tmp = top;

						assert(!top->elems.empty());

						top = top->elems.back();
						top->tag = TagHide;
						top->suffix = tmp->suffix;
					}

					src_suffix = top->suffix;
					src_index = top->index;
					dst_suffix = node->suffix;
					dst_index = 0;

					/*
					 * We should update the source information if it's a list
					 * type and it's elems is not empty.
					 */
					if (top->tag == TagList) {
						if (!top->elems.empty()) {
							node_t *prev = top->elems.back();

							src_suffix = prev->suffix;
							src_index = 0;
						}
					}

					edgeinfo = get_dot_edge(src_suffix, src_index,
											dst_suffix, dst_index,
											top->tag == TagList);

					top->edges.push_back(edgeinfo);
					top->elems.push_back(node);
					node->index = top->elems.size();
				}

				nodes_stack.push(node);
				prev_is_item = false;

#ifdef DEBUG
				write_stderr("STACK: node push %s at stack %u\n",
							 node->name.c_str(), nodes_stack.size());
#endif
				break;
			}
		case '}':
			{
				assert(!nodes_stack.empty());

				top = nodes_stack.top();
				nodes_stack.pop();
				prev_is_item = false;

#ifdef DEBUG
				write_stderr("STACK: node pop %s from stack %u\n",
							 top->name.c_str(), nodes_stack.size());
#endif
				if (nodes_stack.empty()) {
					return top;
				}

				break;
			}
		case '(':
			{
				node_t *node;

				assert(!nodes_stack.empty());

				top = nodes_stack.top();

				assert(!top->elems.empty());

				node = top->elems.back();
				node->tag = TagList;
				node->suffix = top->suffix;

				nodes_stack.push(node);
				prev_is_item = false;

#ifdef DEBUG
				write_stderr("STACK: list push %s at stack %u\n",
							 top->name.c_str(), nodes_stack.size());
#endif
				break;
			}
		case ')':
			{
				assert(!nodes_stack.empty());

				top = nodes_stack.top();
				nodes_stack.pop();
				prev_is_item = false;

#ifdef DEBUG
				write_stderr("STACK: list pop %s from stack %u\n",
							 top->name.c_str(), nodes_stack.size());
#endif

				break;
			}
		case ':':
			{
				node_t *node = new node_t();

				assert(!nodes_stack.empty());

				node->tag = TagItem;
				node->name = get_pg_node_name(fp);
				node->suffix = node_suffix++;

				/* get top node and push current node in its elems */
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

	return NULL;
}

static string
get_pg_node_name(FILE *fp)
{
	int ch;
	string name;

	while ((ch = getc(fp))) {
		if (ch == ':' || ch == '{' || ch == '}') {
			break;
		} else if (ch == '(') {
			/*
			 * Try to get the next non-space character to determine how
			 * to deal with a left parenthesis.
			 * A left parenthesis following a left brace means this is a
			 * list.
			 */
			char tmp = getc(fp);
			while (isspace(tmp)) {
				tmp = getc(fp);
			}

			if (tmp == '{') {
				ungetc(tmp, fp);
				break;
			}

			/* part of the name, remove spaces and continue */
			ungetc(tmp, fp);
		}

		name.push_back(ch);
	}

	/* push back the token */
	ungetc(ch, fp);

	/*
	 * Trim leading and trailing spaces and remove any illegal characters
	 * of dot language.
	 */
	name = trim(name);
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
get_dot_edge(size_t src_suffix, size_t src_index,
			 size_t dst_suffix, size_t dst_index, bool list)
{
	char color[64] = { 0 };
	char edge[1024] = { 0 };

	if (enable_color) {
		if (list) {
			snprintf(color, sizeof(color), " [color=blue]");
		} else {
			snprintf(color, sizeof(color), " [color=green]");
		}
	}

	snprintf(edge, sizeof(edge),
			 "node_%lu:f%lu -> node_%lu:f%lu%s;",
			 src_suffix, src_index, dst_suffix, dst_index, color);

	return string(edge);
}

static void
write_dot_script(node_t *root, FILE *fp)
{
	queue<const node_t *> bfs;

	fprintf(fp,
			"digraph PGNodeGraph {\n"
			"node [shape=none];\n"
			"rankdir=LR;\n"
			"size=\"100000,100000\";\n");

	/* Firstly, construct the nodes. */
	bfs.push(root);
	while (!bfs.empty()) {
		string nodeinfo;
		const node_t *parent = bfs.front();

		bfs.pop();
		nodeinfo = get_dot_node_header(parent->suffix, parent->name);
		for (auto it = parent->elems.begin(); it != parent->elems.end(); it++) {
			const node_t *child = *it;
			/*
			 * If this node has one or more children, we should output it as a
			 * separate dot node.
			 */
			if (!child->elems.empty()) {
				bfs.push(child);
			}

			/* Do not show empty fields if enable skip empty. */
			if (!enable_skip_empty || !name_contains_empty(child->name)) {
				nodeinfo += get_dot_node_body(child->index, child->name);
			}
		}
		nodeinfo += get_dot_node_footer();

		if (parent->tag != TagList && parent->tag != TagHide) {
			fprintf(fp, "%s\n", nodeinfo.c_str());
		}
	}

	/* Then, wirte the edges between nodes. */
	bfs.push(root);
	while (!bfs.empty()) {
		const node_t *curr = bfs.front();

		bfs.pop();
		for (auto it = curr->elems.begin(); it != curr->elems.end(); it++) {
			bfs.push(*it);
		}

		for(auto it = curr->edges.begin(); it != curr->edges.end(); it++) {
			fprintf(fp, "%s\n", it->c_str());
		}

		/* Now, we can release the memory. */
		delete curr;
	}

	fprintf(fp, "}\n");
	fflush(fp);
}

static string
get_dot_node_header(size_t suffix, const string& name)
{
	char brcolor[64] = { 0 };
	char bgcolor[64] = { 0 };
	char ftcolor[64] = { 0 };
	char node_header[4096] = { 0 };

	if (enable_color) {
		auto it = node_color_mapping.find(name);
		if (it != node_color_mapping.end()) {

			if (!it->second.bgcolor.empty()) {
				snprintf(bgcolor, sizeof(bgcolor), " bgcolor=\"%s\"",
						 it->second.bgcolor.c_str());
				snprintf(brcolor, sizeof(brcolor), " color=\"%s\"",
						 it->second.bgcolor.c_str());
			}

			if (!it->second.fontcolor.empty()) {
				snprintf(ftcolor, sizeof(ftcolor), " color=\"%s\"",
						 it->second.fontcolor.c_str());
			}
		}
	}

	/* The border color is same as background color. */
	snprintf(node_header, sizeof(node_header),
			 "node_%lu [\n"
			 "  label=<<table border=\"0\" cellspacing=\"0\"%s>\n"
			 "    <tr>\n"
			 "      <td port=\"f0\" border=\"1\"%s>\n"
			 "       <B><font%s>%s</font></B>\n"
			 "      </td>\n"
			 "    </tr>\n",
			 suffix, brcolor, bgcolor, ftcolor, name.c_str());

	return string(node_header);
}

static string
get_dot_node_body(size_t suffix, const string& name)
{
	char node_body[4096] = { 0 };
	string node_name;

	if (name.find("colnames") != string::npos) {
		node_name = format_colnames(name);
	} else {
		node_name = name;
	}

	snprintf(node_body, sizeof(node_body),
			 "    <tr><td port=\"f%lu\" border=\"1\">%s</td></tr>\n",
			 suffix, node_name.c_str());

	return string(node_body);
}

static string
get_dot_node_footer(void)
{
	return string("  </table>>\n];");
}

/*
 * Check if the name contains an empty value.  Here, the empty value
 * here is a NULL pointer represented by "--".
 */
static bool
name_contains_empty(const string& name)
{
	/*
	 * NB: You could define more empty value, if you defined, change the
	 * following code to contains your new empty value checking.
	 */
	return name.find("--") != string::npos;
}

static string
get_dot_filename(const string& pathname)
{
	string dot_suffix(".dot");

	if (dot_directory) {
		size_t found = pathname.find_last_of("/");
		string name = pathname.substr(found + 1);
		return dot_directory + string("/") + name + dot_suffix;
	}

	return pathname + dot_suffix;
}

static string
get_img_filename(const string& pathname)
{
	string img_suffix(string(".") + picture_format);

	if (img_directory) {
		size_t found = pathname.find_last_of("/");
		string name = pathname.substr(found + 1);
		return img_directory + string("/") + name + img_suffix;
	}

	return pathname + img_suffix;
}

static string
format_colnames(const string& name)
{
	string tmp;
	string new_name;

	if (name == "colnames --") {
		return name;
	}

	new_name = "    \n<table border=\"0\" cellspacing=\"0\"> \n";
	size_t pos = name.find("(");
	new_name += "      <tr>\n";
	new_name += "        <td>" + name.substr(0, pos + 1) + "</td>\n";
	new_name += "        <td></td>\n";
	new_name += "      </tr>\n";

	tmp = name.substr(pos + 1);
	tmp = ltrim(tmp);
	while (tmp.find(' ') != string::npos) {
		pos = tmp.find(' ');
		string t = tmp.substr(0, pos);
		t = rtrim(t);

		new_name += "      <tr>\n";
		new_name += "        <td></td>\n";
		new_name += "        <td align=\"left\">" + t + "</td>\n";
		new_name += "      </tr>\n";

		if (pos == string::npos) {
			break;
		}

		tmp = ltrim(tmp.substr(pos + 1));
	}

	if (!tmp.empty()) {
		new_name += "      <tr>\n";
		new_name += "        <td>" + tmp + "</td>\n";
		new_name += "        <td></td>\n";
		new_name += "      </tr>\n";
	}

	new_name += "    </table>\n";

	return new_name;
}
