/* -*- mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */

/*
 *  Main authors:
 *     Guido Tack <guido.tack@monash.edu>
 */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <minizinc/prettyprinter.hh>
#include <minizinc/htmlprinter.hh>

#include <minizinc/model.hh>
#include <minizinc/eval_par.hh>
#include <minizinc/copy.hh>

#include <sstream>

namespace MiniZinc {

  namespace HtmlDocOutput {
    
    class DocItem {
    public:
      enum DocType { T_PAR=0, T_VAR=1, T_FUN=2 };
      DocItem(const DocType& t0, std::string id0, std::string doc0) : t(t0), id(id0), doc(doc0) {}
      DocType t;
      std::string id;
      std::string doc;
    };

    class Group;
    typedef UNORDERED_NAMESPACE::unordered_map<std::string, Group> GroupMap;

    class Group {
    public:
      Group(const std::string& name0) : name(name0) {}
      std::string name;
      std::string desc;
      std::string htmlName;
      GroupMap subgroups;
      std::vector<DocItem> items;
      std::string toHTML(int level = 0) {
        std::ostringstream oss;

        oss << "<div class='mzn-group-level-" << level << "'>\n";
        if (!htmlName.empty()) {
          oss << "<div class='mzn-group-name'>" << htmlName << "</div>\n";
          oss << "<div class='mzn-group-desc'>\n" << desc << "</div>\n";
        }
        
        for (GroupMap::iterator it = subgroups.begin(); it != subgroups.end(); ++it) {
          oss << it->second.toHTML(level+1);
        }

        struct SortById {
          bool operator ()(const DocItem& i0, const DocItem& i1) {
            return i0.t < i1.t || (i0.t==i1.t && i0.id < i1.id);
          }
        } _cmp;
        std::stable_sort(items.begin(), items.end(), _cmp);

        int cur_t = -1;
        const char* dt[] = {"par","var","fun"};
        const char* dt_desc[] = {"Parameters","Variables","Functions and Predicates"};
        for (std::vector<DocItem>::const_iterator it = items.begin(); it != items.end(); ++it) {
          if (it->t != cur_t) {
            if (cur_t != -1)
              oss << "</div>\n";
            cur_t = it->t;
            oss << "<div class='mzn-decl-type-" << dt[cur_t] << "'>\n";
            oss << "<div class='mzn-decl-type-heading'>" << dt_desc[cur_t] << "</div>\n";
          }
          oss << it->doc;
        }
        if (cur_t != -1)
          oss << "</div>\n";
        oss << "</div>";
        return oss.str();
      }
    };
    
    void addToGroup(GroupMap& gm, const std::string& group, DocItem& di) {
      std::vector<std::string> subgroups;
      size_t lastpos = 0;
      size_t pos = group.find(".");
      while (pos != std::string::npos) {
        subgroups.push_back(group.substr(lastpos, pos-lastpos));
        lastpos = pos+1;
        pos = group.find(".", lastpos);
      }
      subgroups.push_back(group.substr(lastpos, std::string::npos));
      
      GroupMap* cgm = &gm;
      std::string gprefix;
      for (unsigned int i=0; i<subgroups.size(); i++) {
        if (i > 0)
          gprefix += ".";
        gprefix += subgroups[i];
        if (cgm->find(subgroups[i]) == cgm->end()) {
          cgm->insert(std::make_pair(subgroups[i],Group(gprefix)));
        }
        Group& g = cgm->find(subgroups[i])->second;
        if (i==subgroups.size()-1) {
          g.items.push_back(di);
        } else {
          cgm = &g.subgroups;
        }
      }
    }
    
    void setGroupDesc(GroupMap& gm, const std::string& group, std::string htmlName, std::string s) {
      std::vector<std::string> subgroups;
      size_t lastpos = 0;
      size_t pos = group.find(".");
      while (pos != std::string::npos) {
        subgroups.push_back(group.substr(lastpos, pos-lastpos));
        lastpos = pos+1;
        pos = group.find(".", lastpos);
      }
      subgroups.push_back(group.substr(lastpos, std::string::npos));

      GroupMap* cgm = &gm;
      std::string gprefix;
      for (unsigned int i=0; i<subgroups.size(); i++) {
        if (i > 0)
          gprefix += ".";
        gprefix += subgroups[i];
        if (cgm->find(subgroups[i]) == cgm->end()) {
          cgm->insert(std::make_pair(subgroups[i],Group(gprefix)));
        }
        Group& g = cgm->find(subgroups[i])->second;
        if (i==subgroups.size()-1) {
          if (!g.htmlName.empty()) {
            std::cerr << "Warning: two descriptions for group `" << group << "'\n";
          }
          g.htmlName = htmlName;
          g.desc = s;
        } else {
          cgm = &g.subgroups;
        }
      }
    }
    
  }
  
  class PrintHtmlVisitor : public ItemVisitor {
  protected:
    HtmlDocOutput::GroupMap& _gm;
    
    std::string extractArgWord(std::string& s, size_t n) {
      size_t start = n;
      while (start < s.size() && s[start]!=' ' && s[start]!='\t')
        start++;
      while (start < s.size() && (s[start]==' ' || s[start]=='\t'))
        start++;
      int end = start+1;
      while (end < s.size() && (isalnum(s[end]) || s[end]=='_' || s[end]=='.'))
        end++;
      std::string ret = s.substr(start,end-start);
      s = s.substr(0,n)+s.substr(end,std::string::npos);
      return ret;
    }
    std::pair<std::string,std::string> extractArgLine(std::string& s, size_t n) {
      size_t start = n;
      while (start < s.size() && s[start]!=' ' && s[start]!='\t')
        start++;
      while (start < s.size() && (s[start]==' ' || s[start]=='\t'))
        start++;
      int end = start+1;
      while (end < s.size() && s[end]!=':')
        end++;
      std::string arg = s.substr(start,end-start);
      size_t doc_start = end+1;
      while (end < s.size() && s[end]!='\n')
        end++;
      std::string ret = s.substr(doc_start,end-doc_start);
      s = s.substr(0,n)+s.substr(end,std::string::npos);
      return make_pair(arg,ret);
    }
    
    std::vector<std::string> replaceArgs(std::string& s) {
      std::vector<std::string> replacements;
      std::ostringstream oss;
      size_t lastpos = 0;
      size_t pos = std::min(s.find("\\a"), s.find("\\p"));
      size_t mathjax_open = s.find("\\(");
      size_t mathjax_close = s.rfind("\\)");
      if (pos == std::string::npos)
        return replacements;
      while (pos != std::string::npos) {
        oss << s.substr(lastpos, pos-lastpos);
        size_t start = pos;
        while (start < s.size() && s[start]!=' ' && s[start]!='\t')
          start++;
        while (start < s.size() && (s[start]==' ' || s[start]=='\t'))
          start++;
        int end = start+1;
        while (end < s.size() && (isalnum(s[end]) || s[end]=='_'))
          end++;
        if (s[pos+1]=='a') {
          replacements.push_back(s.substr(start,end-start));
          if (pos >= mathjax_open && pos <= mathjax_close) {
            oss << "{\\bf " << replacements.back() << "}";
          } else {
            oss << "<span class='mzn-arg'>" << replacements.back() << "</span>";
          }
        } else {
          if (pos >= mathjax_open && pos <= mathjax_close) {
            oss << "{\\bf " << s.substr(start,end-start) << "}";
          } else {
            oss << "<span class='mzn-parm'>" << s.substr(start,end-start) << "</span>";
          }
        }
        lastpos = end;
        pos = std::min(s.find("\\a", lastpos), s.find("\\p", lastpos));
      }
      oss << s.substr(lastpos, std::string::npos);
      s = oss.str();
      return replacements;
    }
    
    std::string addHTML(const std::string& s) {
      std::ostringstream oss;
      size_t lastpos = 0;
      size_t pos = s.find('\n');
      bool inUl = false;
      oss << "<p>\n";
      while (pos != std::string::npos) {
        oss << s.substr(lastpos, pos-lastpos);
        size_t next = std::min(s.find('\n', pos+1),s.find('-', pos+1));
        if (next==std::string::npos) {
          lastpos = pos+1;
          break;
        }
        bool allwhite = true;
        for (size_t cur = pos+1; cur < next; cur++) {
          if (s[cur]!=' ' && s[cur]!='\t') {
            allwhite = false;
            break;
          }
        }
        if (allwhite) {
          if (s[next]=='-') {
            if (!inUl) {
              oss << "<ul>\n";
              inUl = true;
            }
            oss << "<li>";
          } else {
            if (inUl) {
              oss << "</ul>\n";
              inUl = false;
            } else {
              oss << "</p><p>\n";
            }
          }
          lastpos = next+1;
          pos = s.find('\n', lastpos);
        } else {
          lastpos = pos+1;
          if (s[pos]=='\n') {
            oss << " ";
          }
          if (s[next]=='-') {
            pos = s.find('\n', next+1);
          } else {
            pos = next;
          }
        }
      }
      oss << s.substr(lastpos, std::string::npos);
      if (inUl)
        oss << "</ul>\n";
      oss << "</p>\n";
      return oss.str();
    }
    
  public:
    PrintHtmlVisitor(HtmlDocOutput::GroupMap& gm) : _gm(gm) {}
    void enterModel(Model* m) {
      const std::string& dc = m->docComment();
      if (!dc.empty()) {
        size_t gpos = dc.find("@groupdef");
        while (gpos != std::string::npos) {
          size_t start = gpos;
          while (start < dc.size() && dc[start]!=' ' && dc[start]!='\t')
            start++;
          while (start < dc.size() && (dc[start]==' ' || dc[start]=='\t'))
            start++;
          size_t end = start+1;
          while (end < dc.size() && (isalnum(dc[end]) || dc[end]=='_' || dc[end]=='.'))
            end++;
          std::string groupName = dc.substr(start,end-start);
          size_t doc_start = end+1;
          while (end < dc.size() && dc[end]!='\n')
            end++;
          std::string groupHTMLName = dc.substr(doc_start,end-doc_start);
          
          size_t next = dc.find("@groupdef", gpos+1);
          HtmlDocOutput::setGroupDesc(_gm, groupName, groupHTMLName,
                                      addHTML(dc.substr(end, next == std::string::npos ? next : next-end)));
          gpos = next;
        }
      }
    }
    /// Visit variable declaration
    void vVarDeclI(VarDeclI* vdi) {
      if (Call* docstring = Expression::dyn_cast<Call>(getAnnotation(vdi->e()->ann(), constants().ann.doc_comment))) {
        std::string ds = eval_string(docstring->args()[0]);
        std::string group("main");
        size_t group_idx = ds.find("@group");
        if (group_idx!=std::string::npos) {
          group = extractArgWord(ds, group_idx);
        }
        
        std::ostringstream os;
        os << "<div class='mzn-vardecl'>\n";
        os << "<div class='mzn-vardecl-code'>\n";
        if (vdi->e()->ti()->type() == Type::ann()) {
          os << "<span class='mzn-kw'>annotation</span> ";
          os << "<span class='mzn-fn-id'>" << *vdi->e()->id() << "</span>";
        } else {
          os << *vdi->e()->ti() << ": " << *vdi->e()->id();
        }
        os << "</div><div class='mzn-vardecl-doc'>\n";
        os << addHTML(ds);
        os << "</div>";
        GCLock lock;
        HtmlDocOutput::DocItem di(vdi->e()->type().ispar() ? HtmlDocOutput::DocItem::T_PAR: HtmlDocOutput::DocItem::T_VAR,
                                  vdi->e()->type().toString()+" "+vdi->e()->id()->str().str(), os.str());
        HtmlDocOutput::addToGroup(_gm, group, di);
      }
    }
    /// Visit function item
    void vFunctionI(FunctionI* fi) {
      if (Call* docstring = Expression::dyn_cast<Call>(getAnnotation(fi->ann(), constants().ann.doc_comment))) {
        std::string ds = eval_string(docstring->args()[0]);
        std::string group("main");
        size_t group_idx = ds.find("@group");
        if (group_idx!=std::string::npos) {
          group = extractArgWord(ds, group_idx);
        }

        size_t param_idx = ds.find("@param");
        std::vector<std::pair<std::string,std::string> > params;
        while (param_idx != std::string::npos) {
          params.push_back(extractArgLine(ds, param_idx));
          param_idx = ds.find("@param");
        }
        
        std::vector<std::string> args = replaceArgs(ds);
        
        UNORDERED_NAMESPACE::unordered_set<std::string> allArgs;
        for (unsigned int i=0; i<args.size(); i++)
          allArgs.insert(args[i]);
        for (unsigned int i=0; i<params.size(); i++)
          allArgs.insert(params[i].first);
        
        GCLock lock;
        for (unsigned int i=0; i<fi->params().size(); i++) {
          if (allArgs.find(fi->params()[i]->id()->str().str()) == allArgs.end()) {
            std::cerr << "Warning: parameter " << *fi->params()[i]->id() << " not documented for function "
                      << fi->id() << " at location " << fi->loc() << "\n";
          }
        }
        
        std::ostringstream os;
        os << "<div class='mzn-fundecl'>\n";
        os << "<div class='mzn-fundecl-code'>\n";
        
        fi->ann().remove(docstring);
        
        std::ostringstream fs;
        if (fi->ti()->type() == Type::ann()) {
          fs << "annotation ";
          os << "<span class='mzn-kw'>annotation</span> ";
        } else if (fi->ti()->type() == Type::parbool()) {
          fs << "test ";
          os << "<span class='mzn-kw'>test</span> ";
        } else if (fi->ti()->type() == Type::varbool()) {
          fs << "predicate ";
          os << "<span class='mzn-kw'>predicate</span> ";
        } else {
          fs << "function " << *fi->ti() << ": ";
          os << "<span class='mzn-kw'>function</span> <span class='mzn-ti'>" << *fi->ti() << "</span>: ";
        }
        fs << fi->id() << "(";
        os << "<span class='mzn-fn-id'>" << fi->id() << "</span>(";
        size_t align = fs.str().size();
        for (unsigned int i=0; i<fi->params().size(); i++) {
          fs << *fi->params()[i]->ti() << ": " << *fi->params()[i]->id();
          if (i < fi->params().size()-1) {
            fs << ", ";
          }
        }
        bool splitArgs = (fs.str().size() > 70);
        for (unsigned int i=0; i<fi->params().size(); i++) {
          os << "<span class='mzn-ti'>" << *fi->params()[i]->ti() << "</span>: "
             << "<span class='mzn-id'>" << *fi->params()[i]->id() << "</span>";
          if (i < fi->params().size()-1) {
            os << ",";
            if (splitArgs) {
              os << "\n";
              for (unsigned int j=align; j--;)
                os << " ";
            } else {
              os << " ";
            }
          }
        }
        os << ")";
        
        fi->ann().add(docstring);
        os << "</div>\n<div class='mzn-fundecl-doc'>\n";
        std::string dshtml = addHTML(ds);
        if (dshtml.find("rightjust") != std::string::npos) {
          std::cerr << "found it\n";
          std::cerr << ds << "\n" << dshtml << "\n";
        }

        os << dshtml;
        if (params.size() > 0) {
          os << "<div class='mzn-fundecl-params-heading'>Parameters</div>\n";
          os << "<ul class='mzn-fundecl-params'>\n";
          for (unsigned int i=0; i<params.size(); i++) {
            os << "<li>" << params[i].first << ": " << params[i].second << "</li>\n";
          }
          os << "</ul>\n";
        }
        os << "</div>";
        os << "</div>";

        HtmlDocOutput::DocItem di(HtmlDocOutput::DocItem::T_FUN, fi->id().str(), os.str());
        HtmlDocOutput::addToGroup(_gm, group, di);
      }
    }
  };
  
  std::vector<HtmlDocument>
  HtmlPrinter::printHtml(MiniZinc::Model* m) {
    using namespace HtmlDocOutput;
    GroupMap gm;
    PrintHtmlVisitor phv(gm);
    iterItems(phv, m);
    std::vector<HtmlDocument> ret;
    for (GroupMap::iterator it = gm.begin(); it != gm.end(); ++it) {
      ret.push_back(HtmlDocument(it->second.name, it->second.toHTML()));
    }
    return ret;
  }
  
  HtmlDocument
  HtmlPrinter::printHtmlSinglePage(MiniZinc::Model* m) {
    using namespace HtmlDocOutput;
    GroupMap gm;
    PrintHtmlVisitor phv(gm);
    iterItems(phv, m);
    std::ostringstream oss;
    for (GroupMap::iterator it = gm.begin(); it != gm.end(); ++it) {
      oss << it->second.toHTML();
    }
    return HtmlDocument("model.html", oss.str());
  }
 
  void
  HtmlPrinter::htmlHeader(std::ostream& os, const std::string& title) {
    os << "<!doctype html>\n";
    
    os << "<html lang='en'>\n";
    os << "<head>\n";
    os << "<meta charset='utf-8'>\n";
    os << "<link rel='stylesheet' type='text/css' href='style.css'>\n";
    os << "<title>" << title << "</title>\n";
    os << "<script type='text/javascript' src='http://cdn.mathjax.org/mathjax/latest/MathJax.js?config=TeX-AMS-MML_HTMLorMML'></script>\n";
    
    os << "</head>\n";
    
    os << "<body>\n";
  }

  void
  HtmlPrinter::htmlFooter(std::ostream& os) {
    os << "</body>\n";
    os << "</html>\n";
  }

}