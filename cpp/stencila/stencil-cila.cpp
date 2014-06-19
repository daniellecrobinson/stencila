#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/xpressive/xpressive_static.hpp>
#include <boost/xpressive/regex_compiler.hpp>

#include <stencila/stencil.hpp>

// Anonymous local namespace for Cila parsing and generating functions
// which do not need to be Stencil methods
namespace {

using namespace Stencila;
typedef Stencil::Node Node;

// Forward declarations of the main parsing and generating functions
// which are used recursively below
void parse(Node node, std::istream& stream);
void generate(Node node, std::ostream& stream, std::string indent);

// Enumeration for the Cila parsing mode
enum Mode {
    normal_mode,
    code_mode
};

// Parsing state information passed between parsing functions
struct State {
    Mode mode = normal_mode;

    bool end = false;

    struct Line {
        bool empty = false;
        int indentation = 0;
    };
    Line current;
    Line previous;

    struct {
        std::string lang;
        std::string content;
        int indentation = 0;
    } code;
};

void code_mode_start(const std::string& lang, State& state){
    // Set state variables (note indentation to one plus current) 
    state.mode = code_mode;
    state.code.lang = lang;
    state.code.content = "";
    state.code.indentation = state.current.indentation + 1;
}

void code_mode_check(Node parent, const std::string& line, State& state){
    // If line is empty or indented more...
    if(not state.end and (state.current.empty or state.current.indentation>=state.code.indentation)){
        // Add line, even if it is empty, to `code.content`
        // But strip `code.indentation` from it first
        if(state.current.empty) state.code.content += line;
        else state.code.content += line.substr(state.code.indentation);
        state.code.content += "\n";
    }
    // Otherwise wrap up the code element
    else {
        // The following block of code escapes the CDATA wrapper
        // It seems unecessary to do this, certainly for non-<script> elements
        // which the browser wil no attempt to parse anyway
        #if 0
            // Determine comment flag
            std::string comment;
            if(code_lang=="r" or code_lang=="py") comment = "#";
            else comment = "//";
            // Add a comment token to escape "<![CDATA[" . This needs to be added to `current` node NOT `state.code.content`
            current.append_text(comment);
            // Add a comment token to escape "]]>". This needs to be added to `state.code.content` before appending as CDATA
            state.code.content += comment;
        #endif
        // Insert a starting newline to escape the "<![CDATA[" line
        state.code.content.insert(0,"\n");
        // Then add the code as CDATA
        parent.append_cdata(state.code.content);
        // Turn off code mode
        state.mode = normal_mode;
    }
}

// Language grammer is defined below.
// For clarity, parsing and generating functions
// are defined adjacent to each element of the grammar
using namespace boost::xpressive;

/**
 * Text
 */

sregex text = +_;

Node text_parse(Node parent, const smatch& tree, const State& state){
    // If previous line was empty then create a new paragraph to be target
    // for additional text, otherwise use existing parent as target
    Node node;
    if(state.previous.empty){
        node = parent.append("p");
    } else {
        node = parent;
    }
    // Text nodes may have "inlines" defined using curly braces e.g.
    //   The minimum is {if a<b {text a} else {text b}}.
    // Deal with those by replacing { with indented lines and } with outdented lines
    std::string content = tree.str();
    bool altered = false;
    std::string formatted;
    formatted.reserve(content.length());
    std::deque<char> indent = {'\n'};
    for(std::string::iterator iter=content.begin();iter!=content.end();iter++){
        char chara = *iter;
        if(chara=='{'){
            altered = true;
            // Add a newline with indentation if there is already some content
            if(formatted.length()>0) formatted.append(indent.begin(),indent.end());
            indent.push_back('\t');
        }
        else if(chara=='}'){
            formatted.push_back('\n');
            indent.pop_back();
        }
        else {
            formatted += chara;
        }
    }
    // Remove any trailing newline
    if(formatted.back()=='\n') formatted.pop_back();

    if(not altered) node.append_text(content);
    else {
        std::stringstream stream(formatted);
        stream.seekg(0);
        //std::cout<<"\n--------------------------\n"<<formatted<<"\n--------------------------"<<std::endl;
        parse(node,stream);
        //std::cout<<"\n~~~~~~~~~~~~~~~~~~~~~~~~~~\n"<<node.dump()<<"\n~~~~~~~~~~~~~~~~~~~~~~~~~~"<<std::endl;
    }
    return node;
}

/**
 * HTML5 tags
 */
/*
List of vaild HTML5 element names from 
    http://www.w3.org/TR/html-markup/elements.html
and extracted using this python script:
    import requests
    import bs4
    page = requests.get('http://www.w3.org/TR/html-markup/elements.html').text
    elems = bs4.BeautifulSoup(page).findAll('span', {'class':'element'})
    print '|'.join('"%s"'%elem.text for elem in sorted(set(elems)))
*/
sregex tag = 
    #if 0 
    /*
    Statically compiling element name list dramatically increases compile
    times (e.g. 11s to 27s) and executable sizes (e.g. 10Mb to 80Mb).
    */
    as_xpr("a")|"abbr"|"address"|"area"|"article"|"aside"|"audio"|"b"|"base"|"bdi"|"bdo"|"blockquote"|"body"|"br"|"button"|
        "canvas"|"caption"|"cite"|"code"|"col"|"colgroup"|"command"|"datalist"|"dd"|"del"|"details"|"dfn"|"div"|"dl"|"dt"|
        "em"|"embed"|"fieldset"|"figcaption"|"figure"|"footer"|"form"|"h1"|"h2"|"h3"|"h4"|"h5"|"h6"|"head"|"header"|"hgroup"|"hr"|"html"|
        "i"|"iframe"|"img"|"input"|"ins"|"kbd"|"keygen"|"label"|"legend"|"li"|"link"|"map"|"mark"|"menu"|"meta"|"meter"|"nav"|"noscript"|
        "object"|"ol"|"optgroup"|"option"|"output"|"p"|"param"|"pre"|"progress"|"q"|"rp"|"rt"|"ruby"|"s"|"samp"|"script"|"section"|
        "select"|"small"|"source"|"span"|"strong"|"style"|"sub"|"summary"|"sup"|"table"|"tbody"|"td"|"textarea"|"tfoot"|"th"|"thead"|
        "time"|"title"|"tr"|"track"|"u"|"ul"|"var"|"video"|"wbr"
    #else
    /*
    Dynamically compiling element name list only slightly increases compile
    times (e.g. 11s to 15s) and executable sizes (e.g. 10Mb to 13Mb).
    */
    sregex::compile(
        "a|abbr|address|area|article|aside|audio|b|base|bdi|bdo|blockquote|body|br|button|"
        "canvas|caption|cite|code|col|colgroup|command|datalist|dd|del|details|dfn|div|dl|dt|"
        "em|embed|fieldset|figcaption|figure|footer|form|h1|h2|h3|h4|h5|h6|head|header|hgroup|hr|html|"
        "i|iframe|img|input|ins|kbd|keygen|label|legend|li|link|map|mark|menu|meta|meter|nav|noscript|"
        "object|ol|optgroup|option|output|p|param|pre|progress|q|rp|rt|ruby|s|samp|script|section|"
        "select|small|source|span|strong|style|sub|summary|sup|table|tbody|td|textarea|tfoot|th|thead|"
        "time|title|tr|track|u|ul|var|video|wbr"
    )
    #endif
;

/**
 * Attributes of elements
 */

sregex identifier  = +(_w|'-');

// id="bar"

sregex id = '#' >> identifier;

void id_parse(Node node, const smatch& tree){
    // Set `id` after removing leading "#"
    // This will overide any previous id assignments for this element
    node.attr("id",tree.str(0).erase(0,1));
}

void id_gen(Node node, std::ostream& stream){
    auto id = node.attr("id");
    if(id.length()){
        // Id is reduntant if this is a macro so do not output id
        // if this node is a macro
        auto macro = node.attr("data-macro");
        if(macro.length()==0) stream<<"#"<<id;
    };
}

// class="foo"

sregex class_ = '.' >> identifier;

void class_parse(Node node, const smatch& tree){
    // Concatenate to `class` after removing leading "."
    // This will "accumulate" any class assignments
    node.concat("class",tree.str(0).erase(0,1));
}

void class_gen(Node node, std::ostream& stream){
    // Get clas attribute and split using spaces
    std::string class_ = node.attr("class");
    if(class_.length()){
        std::vector<std::string> classes;
        boost::split(classes,class_,boost::is_any_of(" "));
        for(std::string class_ : classes){
            if(class_.length()) stream<<"."<<class_;
        }
    }
}

// [foo="bar"]

sregex string = ('\"' >> *(~(set='\r','\n','\"')) >> '\"') | 
                ('\'' >> *(~(set='\r','\n','\'')) >> '\'');
sregex attr_assign = '[' >> *space >> identifier >> '=' >> string >> *space >> ']';

void attr_assign_parse(Node node, const smatch& tree){
    // Get name and value
    auto branch = tree.nested_results().begin();
    auto name = branch;
    auto value = ++branch;
    // Remove leading and trailing quotes from value
    std::string content = value->str();
    content.erase(0,1);
    content.erase(content.length()-1,1);
    // Set the attribute
    node.attr(name->str(),content);
}

void attr_assign_gen(Node node, std::ostream& stream, const std::string& attr){
    stream << "[" << attr << "=\"" + node.attr(attr) + "\"]";
}

/**
 * off: Indicator for conditional stencil directives
 * (if,elif,else,case,default)
 */

sregex off = as_xpr('/');

void off_parse(Node node, const smatch& tree){
    // Set the attribute to true
    node.attr("data-off","true");
}

void off_gen(Node node, std::ostream& stream){
    auto off = node.attr("data-off");
    if(off.length()) stream<<"/";
}

/**
 * index: Indicator for index of each elements in 
 * a for directive
 */

sregex index = as_xpr('@') >> +_d;

void index_parse(Node node, const smatch& tree){
    // Set the attribute, removing the leading @
    node.attr("data-index",tree.str().substr(1));
}

void index_gen(Node node, std::ostream& stream){
    auto index = node.attr("data-index");
    if(index.length()) stream<<"@"<<index;
}

/**
 * lock: Indicator for elements that have been edited an
 * which should not be overwritten by rendering
 */

sregex lock = as_xpr('^');

void lock_parse(Node node, const smatch& tree){
    // Set the attribute to true
    node.attr("data-lock","true");
}

void lock_gen(Node node, std::ostream& stream){
    auto lock = node.attr("data-lock");
    if(lock.length()) stream<<"^";
}

/**
 * Stencil directives
 */

// Regexes for the types of directive arguments
// Currently, very permissive
sregex expr = +_;
sregex address = +_w;
sregex selector = +_;

/**
 * Directives with no arguments
 */
sregex directive_noarg = sregex::compile("each|else|default");

void directive_noarg_parse(Node node, const smatch& tree){
    // Set empty directive attribute
    node.attr("data-"+tree.str(),"");
}

void directive_noarg_gen(const std::string type, Node node, std::ostream& stream){
    stream<<type;
}

/**
 * Directives with a single expression argument
 */
sregex directive_expr_name = sregex::compile("text|with|if|elif|switch|case");
sregex directive_expr = directive_expr_name >> +space >> expr;

void directive_expr_parse(Node node, const smatch& tree){
    // Get name and value
    auto branch = tree.nested_results().begin();
    auto name = branch;
    auto value = ++branch;
    // Set directive attribute
    node.attr("data-"+name->str(),value->str());
}

void directive_expr_gen(const std::string type, Node node, std::ostream& stream){
    stream<<type<<" "<<node.attr("data-"+type);
}

/**
 * For directive
 */

sregex for_ = as_xpr("for") >> +space >> expr >> +space >> "in" >> +space >> expr;

void for_parse(Node node, const smatch& tree){
    // Get item and items
    auto branch = tree.nested_results().begin();
    auto item = branch;
    auto items = ++branch;
    // Set for attribute
    node.attr("data-for",item->str()+":"+items->str());
}

void for_gen(Node node, std::ostream& stream){
    auto parts = node.attr("data-for");
    auto colon = parts.find_first_of(":");
    std::string item = "item";
    std::string items = "items";
    if(colon!=std::string::npos){
        item = parts.substr(0,colon);
        if(item.length()==0) STENCILA_THROW(Exception,"Missing 'item' parameter")
        items = parts.substr(colon+1);
        if(items.length()==0) STENCILA_THROW(Exception,"Missing 'items' parameter")
    } else {
        STENCILA_THROW(Exception,"Missing semicolon")
    }
    stream<<"for "<<item<<" in "<<items;
}

/**
 * Include directive
 */

sregex include = as_xpr("include") >> +space >> address >> *(+space >> selector);

void include_parse(Node node, const smatch& tree){
    auto include = tree.nested_results().begin();
    node.attr("data-include",include->str());
    auto select = ++include;
    if(select!=tree.nested_results().end()) node.attr("data-select",select->str());
}

void include_gen(Node node, std::ostream& stream){
    stream<<"include "<<node.attr("data-include");
    auto select = node.attr("data-select");
    if(select.length()) stream<<" "<<select;
}

/**
 * Modifier directives
 */

sregex modifier_name = sregex::compile("delete|replace|change|before|after|prepend|append");
sregex modifier = modifier_name >> +space >> selector;

void modifier_parse(Node node, const smatch& tree){
    auto branch = tree.nested_results().begin();
    auto which = branch->str();
    auto selector = (++branch)->str();
    node.attr("data-"+which,selector);
}

void modifier_gen(const std::string& which, Node node, std::ostream& stream){
    auto selector = node.attr("data-"+which);
    stream<<which<<" "<<selector;
}

/**
 * macro directive
 */

sregex macro = as_xpr("macro") >> +space >> identifier;

void macro_parse(Node node, const smatch& tree){
    auto name = tree.nested_results().begin()->str();
    node.attr("data-macro",name);
    node.attr("id",name);
}

void macro_gen(Node node, std::ostream& stream){
    stream<<"macro "<<node.attr("data-macro");
}

/**
 * Element line
 */

sregex element = (
    // These grammar rules are repetitive. But attempting to simplify tem can create a rule that
    // allows nothing before the trailing text which thus implies an extra <div> which is not what is wanted
    (tag >> *(id|class_|attr_assign|off|index|lock) >> !("!" >> (directive_noarg|directive_expr|for_|include|modifier|macro)))|
    (       +(id|class_|attr_assign|off|index|lock) >> !("!" >> (directive_noarg|directive_expr|for_|include|modifier|macro)))|
    (tag                                            >>   "!" >> (directive_noarg|directive_expr|for_|include|modifier|macro) )|
    (                                                            directive_noarg|directive_expr|for_|include|modifier|macro  )
) >> 
    // Allow for trailing text. Note that the first space is not significant (it does
    // not get included in `text`).
    !(space>>*text); 

Node element_parse(Node parent, const smatch& tree, State& state){
    auto branch = tree.nested_results().begin();
    // The first branch is always a tag or an attr
    // If it is an tag use that, otherwise make it a <div>
    std::string name = (branch->regex_id()==tag.regex_id())?branch->str():"div";
    // Create the element
    Node node = parent.append(name);
    // Iterate over remaining branches which include attributes for the element
    // including those for stencil directives.
    // Note that since the first branch may need further processing (if it is an attribute)
    // that the branch iterator is not incremented until the end of the loop.
    while(branch!=tree.nested_results().end()){
        const void* id = branch->regex_id();
        // Attributes
        if(id==::id.regex_id()) id_parse(node,*branch);
        else if(id==class_.regex_id()) class_parse(node,*branch);
        else if(id==attr_assign.regex_id()) attr_assign_parse(node,*branch);
        // Flags
        else if(id==off.regex_id()) off_parse(node,*branch);
        else if(id==index.regex_id()) index_parse(node,*branch);
        else if(id==lock.regex_id()) lock_parse(node,*branch);
        // Directives
        else if(id==directive_noarg.regex_id()) directive_noarg_parse(node,*branch);
        else if(id==directive_expr.regex_id()) directive_expr_parse(node,*branch);
        else if(id==for_.regex_id()) for_parse(node,*branch);
        else if(id==include.regex_id()) include_parse(node,*branch);
        else if(id==modifier.regex_id()) modifier_parse(node,*branch);
        else if(id==macro.regex_id()) macro_parse(node,*branch);
        // Text
        else if(id==text.regex_id()) text_parse(node,*branch,state);
        branch++;
    }
    return node;
}

void element_gen(Node node, std::ostream& stream,const std::string& indent){
    // Unless this is the very first content written to the stream
    // start elements on a new line with appropriate indentation
    if(stream.tellp()>0) stream<<"\n"<<indent;
    // The format of the element line can be complicated and dependent
    // upon the what came earlier on the line so use a string rather than
    // going straight to the stream
    std::ostringstream line;
    // Get element name and attributes
    std::string name = node.name();    
    auto attrs = node.attrs();
    // Check number of attributes
    if(attrs.size()==0) {
        // If this has no attributes then output the node name (this needs to be done for <div>s too 
        // otherwise you get a blank line)
        line << name;
    } else {
        // If this is not a <div> then output name
        if(name!="div") line << name;
        // id
        id_gen(node,line);
        // class
        class_gen(node,line);
        // Other attributes go before directives
        for(std::string attr : node.attrs()){
            if(
                attr!="id" and attr!="class" and 
                attr!="off"  and attr!="index"  and 
                attr!="lock" and attr.substr(0,5)!="data-"
            ){
                attr_assign_gen(node,line,attr);
            }
        }
        // Flags go before directives
        off_gen(node,line);
        index_gen(node,line);
        lock_gen(node,line);
        // Directive attributes. An element can only have one of these.
        // These need to go after the other attributes
        std::ostringstream directive;
        for(std::string attr : attrs){
            if(attr=="data-text") directive_expr_gen("text",node,directive);
            else if(attr=="data-with") directive_expr_gen("with",node,directive);
            else if(attr=="data-if") directive_expr_gen("if",node,directive);
            else if(attr=="data-elif") directive_expr_gen("elif",node,directive);
            else if(attr=="data-else") directive_noarg_gen("else",node,directive);
            else if(attr=="data-switch") directive_expr_gen("switch",node,directive);
            else if(attr=="data-case") directive_expr_gen("case",node,directive);
            else if(attr=="data-default") directive_noarg_gen("default",node,directive);
            else if(attr=="data-for") for_gen(node,directive);
            else if(attr=="data-each") directive_noarg_gen("each",node,directive);
            else if(attr=="data-include") include_gen(node,directive);
            #define MOD_(which) else if(attr=="data-"#which) modifier_gen(#which,node,directive);
                MOD_(delete)
                MOD_(replace)
                MOD_(change)
                MOD_(before)
                MOD_(after)
                MOD_(prepend)
                MOD_(append)
            #undef MOD_
            else if(attr=="data-macro") macro_gen(node,directive);
            // If one of these directives has been hit then add to line
            // and break fro attr loop
            if(directive.tellp()>0){
                if(line.tellp()>0) line << '!';
                line << directive.str();
                break;
            }
        }
    }
    // Add line to the stream
    stream<<line.str();
    // Generate Cila for children
    for(Node child : node.children()) generate(child,stream,indent+"\t");
}

/**
 * Code directive for embedded code
 */
sregex format = as_xpr("out") | "svg" | "png" | "jpg";
sregex size = +_d >> "x" >> +_d;
sregex lang = as_xpr("py") | "r";
sregex code = lang >> *(+space >> (format|size));

Node code_parse(Node parent, const smatch& tree, State& state){
    // The code language is always the first branch
    auto language = tree.nested_results().begin()->str();
    // Append the node
    Node node = parent.append("code",{{"data-code",language}});
    // Iterate over branches adding arguments
    for(auto branch : tree.nested_results()){
        auto id = branch.regex_id();
        if(id==format.regex_id()) node.attr("data-format",branch.str());
        else if(id==size.regex_id()) node.attr("data-size",branch.str());
    }
    // Turn on code mode processing
    code_mode_start(language,state);
    return node;
}

void code_gen(Node node, std::ostream& stream, const std::string& indent){
    // Unless this is the very first content written to the stream
    // start on a new line with appropriate indentation
    if(stream.tellp()>0) stream<<"\n"<<indent;
    // Output language code; no element name
    stream<<node.attr("data-code");
    //Optional arguments
    for(auto attr : {"data-format","data-size"}){
        if(node.attr(attr).length()){
            stream<<" "<<node.attr(attr);
        }
    }
    stream<<"\n";
    // Get the actual code. Assumes it is the first child node!
    auto code = node.first().text();
    // Split into lines
    std::vector<std::string> lines;
    boost::split(lines,code,boost::is_any_of("\n"));
    // Add extra indentation to each line
    // Ignore the first line which was added when parsing Cila to HTML
    // and the last line which is a "pseudo" line created when splittin by "\n"
    for(uint index=1;index<(lines.size()-1);index++){
        stream<<indent+"\t"<<lines[index];
        // Don't put a newline on last line - that is the 
        // repsonsibility of the following element
        if(index<(lines.size()-2)) stream<<"\n";
    }
}

/**
 * Equations
 */

sregex asciimath = as_xpr("`") >> *(~(set='`')) >> as_xpr("`");
sregex tex = as_xpr("\\(") >> *_ >> as_xpr("\\)");
sregex equation = asciimath|tex;

Node equation_parse(Node parent,const smatch& tree){
    // Resolve type
    std::string type;
    auto branch = tree.nested_results().begin();
    auto id = branch->regex_id();
    if(id==asciimath.regex_id()){
        type = "asciimath";
    }
    else if(id==tex.regex_id()){
        type = "tex";
    }
    // Create a <p class="<type>">
    Node node = parent.append("p",{{"class",type}});
    // Append equation text to current node with delimiters
    node.append_text(tree.str());
    return node;
}

void equation_gen(Node node, std::ostream& stream, const std::string& indent){
    // Just need to output child text, not the paragraph element
    // No extra indentation required
    for(Node child : node.children()) generate(child,stream,indent);
}

/**
 * Comments.
 * Currently not implemented
 */

sregex comment_text = *_;
sregex comment = as_xpr("//") >> comment_text;

/**
 * Root regex for each line
 */
sregex root = comment|equation|code|element|text;

void parse(Node node, std::istream& stream){
    // Keep track of state variables for 
    // sending to other nose-specific parsing functions
    State state;
    // Define a parent node, starting off as the stencil's
    // content root node
    Node parent = node;
    // Define current node that may become parent
    Node current = node;
    // Define a stack of <indent,Node> pairs
    // when the indentation increases then then `currrent`
    // becomes parent
    std::deque<std::pair<int,Node>> levels;
    levels.push_back({0,node});
    // Iterate over lines
    uint count = 0;
    while(true){
        // Read line in
        std::string line;
        bool ok = std::getline(stream,line,'\n');
        // Check for end of file so that can be treated
        // as a "pseudo"-line
        if(not ok){
            if(stream.eof()) state.end = true;
            else break;
        } else {
            // Increment the line counter
            count++;
        }
        // Determine if this line is empty - anything other than whitespace?
        bool empty = state.current.empty = line.find_first_not_of("\t ")==std::string::npos;
        int indentation = state.current.indentation = (not empty)?line.find_first_not_of("\t"):0;

        //std::cout<<count<<":"<<empty<<":"<<indentation<<":"<<state.end<<":"<<line<<"\n";

        // If in `code_mode` then process the line immeadiately
        // and potentially change to `normal_mode`
        // This should be done before any changes to `parent`
        if(state.mode==code_mode) code_mode_check(parent,line,state);
        // Determine the parent-child relationships for this node based on its indentation
        // If indentation has increased, the current node becomes the parent
        int last = levels.back().first;
        if(indentation>last){
            levels.push_back({indentation,current});
            parent = current;
        }
        // if it has not changed, then do nothing
        else if(indentation==last){
        }
        // if it less, then pop off parents until we get to the right level
        else {
            while(levels.size()>1 and indentation<levels.back().first) levels.pop_back();
            parent = levels.back().second;
        }
        // Normal mode processing which may have been turned
        // back on by `code_process` etc
        if(state.mode==normal_mode){
            // If the line has content...
            if(not state.current.empty){
                // Remove indentation before parsing
                std::string content = line.substr(indentation);
                // Parse the line into a syntax tree
                smatch tree;
                regex_match(content,tree,root);
                // Parse the first and only branch of the tree
                auto branch = tree.nested_results().begin();
                const void* id = branch->regex_id();
                if(id==comment.regex_id()) {}
                else if(id==equation.regex_id()) current = equation_parse(parent,*branch);
                else if(id==code.regex_id()) current = code_parse(parent,*branch,state);
                else if(id==element.regex_id()) current = element_parse(parent,*branch,state);
                else if(id==text.regex_id()) current = text_parse(parent,*branch,state);
                else  STENCILA_THROW(Exception,"Unrecognised syntax: "+boost::lexical_cast<std::string>(count)+": "+line);
            }
        }
        // If this is the end then break out,
        // otherwise update state.previous
        if(state.end) break;
        else state.previous = state.current;
    }
}

void generate(Node node, std::ostream& stream, std::string indent="") {
    // Generate Cila for a Stencil::Node
    if(node.is_document()){
        // .... generate Cila for each cild with no indentation
        for(Node child : node.children()) generate(child,stream);
    }
    else if(node.name()=="p" and (node.attr("class")=="asciimath" or node.attr("class")=="tex")) equation_gen(node,stream,indent);
    else if(node.attr("data-code")!="") code_gen(node,stream,indent);
    else if(node.is_element()) element_gen(node,stream,indent);
    else if(node.is_text()){
        std::string text = node.text();
        stream<<"\n"<<indent<<text;
    }
}

} // namespace <anonymous>

namespace Stencila {

// Parsing methods

Stencil& Stencil::cila(const std::string& string){
    // Convert the std::string to a std::istream
    // and pass to cila(std::istream&)
    std::stringstream stream(string);
    stream.seekg(0);
    return cila(stream);
}

Stencil& Stencil::cila(std::istream& stream){
    // Clear the stencil of all existing content
    clear();
    // Parse Cila with this Stencil as the root Node
    parse(*this,stream);
    return *this;
}

// Generating methods

std::string Stencil::cila(void) const {
    // Create a std::ostream and pass to cila(std::ostream)
    std::ostringstream stream;
    cila(stream);
    return stream.str();
}

std::ostream& Stencil::cila(std::ostream& stream) const {
    // Generate Cila with this Stencil as the root Node
    generate(*this,stream);
    return stream;
}

}
