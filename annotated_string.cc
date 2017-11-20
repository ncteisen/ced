// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "annotated_string.h"

AnnotatedString::AnnotatedString() {
  chars_ = chars_
               .Add(Begin(),
                    CharInfo{false, 0, End(), End(), End(), End(), AVL<ID>()})
               .Add(End(), CharInfo{false, 1, Begin(), Begin(), Begin(),
                                    Begin(), AVL<ID>()});
  line_breaks_ = line_breaks_.Add(Begin(), LineBreak{End(), End()})
                     .Add(End(), LineBreak{Begin(), Begin()});
}

ID AnnotatedString::MakeRawInsert(CommandSet* commands, Site* site,
                                  absl::string_view chars, ID after,
                                  ID before) {
  auto ids = site->GenerateIDBlock(chars.length());
  auto cmd = commands->add_commands();
  cmd->set_id(ids.first.id);
  auto ins = cmd->mutable_insert();
  ins->set_after(after.id);
  ins->set_before(before.id);
  ins->set_characters(chars.data(), chars.length());
  return ids.second;
}

void AnnotatedString::MakeDelete(CommandSet* commands, ID id) {
  auto cmd = commands->add_commands();
  cmd->set_id(id.id);
  cmd->mutable_delete_();
}

void AnnotatedString::MakeDelMark(CommandSet* commands, ID id) {
  auto cmd = commands->add_commands();
  cmd->set_id(id.id);
  cmd->mutable_del_mark();
}

void AnnotatedString::MakeDelDecl(CommandSet* commands, ID id) {
  auto cmd = commands->add_commands();
  cmd->set_id(id.id);
  cmd->mutable_del_decl();
}

ID AnnotatedString::MakeDecl(CommandSet* commands, Site* site,
                             const Attribute& attribute) {
  auto cmd = commands->add_commands();
  ID id = site->GenerateID();
  cmd->set_id(id.id);
  *cmd->mutable_decl() = attribute;
  return id;
}

ID AnnotatedString::MakeMark(CommandSet* commands, Site* site,
                             const Annotation& annotation) {
  auto cmd = commands->add_commands();
  ID id = site->GenerateID();
  cmd->set_id(id.id);
  *cmd->mutable_mark() = annotation;
  return id;
}

AnnotatedString AnnotatedString::Integrate(const CommandSet& commands) const {
  AnnotatedString s = *this;
  for (const auto& cmd : commands.commands()) {
    switch (cmd.command_case()) {
      case Command::kInsert:
        s.IntegrateInsert(cmd.id(), cmd.insert());
        break;
      case Command::kDelete:
        s.IntegrateDelChar(cmd.id());
        break;
      case Command::kDecl:
        s.IntegrateDecl(cmd.id(), cmd.decl());
        break;
      case Command::kDelDecl:
        s.IntegrateDelDecl(cmd.id());
        break;
      case Command::kMark:
        s.IntegrateMark(cmd.id(), cmd.mark());
        break;
      case Command::kDelMark:
        s.IntegrateDelMark(cmd.id());
        break;
      default:
        throw std::runtime_error("String integration failed");
    }
  }
  return s;
}

void AnnotatedString::IntegrateInsert(ID id, const InsertCommand& cmd) {
  if (chars_.Lookup(id)) return;
  ID after = cmd.after();
  ID before = cmd.before();
  for (auto c : cmd.characters()) {
    IntegrateInsertChar(id, c, after, before);
    after = id;
    id.clock++;
  }
}

void AnnotatedString::IntegrateInsertChar(ID id, char c, ID after, ID before) {
  for (;;) {
    const CharInfo* caft = chars_.Lookup(after);
    const CharInfo* cbef = chars_.Lookup(before);
    assert(caft != nullptr);
    assert(cbef != nullptr);
    if (caft->next == before) {
      if (c == '\n') {
        auto prev_line_id = after;
        const CharInfo* plic = caft;
        while (prev_line_id != Begin() &&
               (!plic->visible || plic->chr != '\n')) {
          prev_line_id = plic->prev;
          plic = chars_.Lookup(prev_line_id);
        }
        auto prev_lb = line_breaks_.Lookup(prev_line_id);
        auto next_lb = line_breaks_.Lookup(prev_lb->next);
        line_breaks_ =
            line_breaks_.Add(prev_line_id, LineBreak{prev_lb->prev, id})
                .Add(id, LineBreak{prev_line_id, prev_lb->next})
                .Add(prev_lb->next, LineBreak{id, next_lb->next});
      }
      chars_ = chars_
                   .Add(after,
                        CharInfo{caft->visible, caft->chr, id, caft->prev,
                                 caft->after, caft->before, caft->annotations})
                   .Add(id, CharInfo{true, c, before, after, after, before,
                                     caft->annotations})
                   .Add(before,
                        CharInfo{cbef->visible, cbef->chr, cbef->next, id,
                                 cbef->after, cbef->before, cbef->annotations});
      return;
    }
    typedef std::map<ID, const CharInfo*> LMap;
    LMap inL;
    std::vector<typename LMap::iterator> L;
    auto addToL = [&](ID id, const CharInfo* ci) {
      L.push_back(inL.emplace(id, ci).first);
    };
    addToL(after, caft);
    ID n = caft->next;
    do {
      const CharInfo* cn = chars_.Lookup(n);
      assert(cn != nullptr);
      addToL(n, cn);
      n = cn->next;
    } while (n != before);
    addToL(before, cbef);
    size_t i, j;
    for (i = 1, j = 1; i < L.size() - 1; i++) {
      auto it = L[i];
      auto ai = inL.find(it->second->after);
      if (ai == inL.end()) continue;
      auto bi = inL.find(it->second->before);
      if (bi == inL.end()) continue;
      L[j++] = L[i];
    }
    L[j++] = L[i];
    L.resize(j);
    for (i = 1; i < L.size() - 1 && L[i]->first < id; i++)
      ;
    // loop with new bounds
    after = L[i - 1]->first;
    before = L[i]->first;
  }
}

void AnnotatedString::IntegrateDelChar(ID id) {
  const CharInfo* cdel = chars_.Lookup(id);
  if (!cdel->visible) return;
  if (cdel->chr == '\n') {
    auto* self = line_breaks_.Lookup(id);
    auto* prev = line_breaks_.Lookup(self->prev);
    auto* next = line_breaks_.Lookup(self->next);
    line_breaks_ = line_breaks_.Remove(id)
                       .Add(self->prev, LineBreak{prev->prev, self->next})
                       .Add(self->next, LineBreak{self->prev, next->next});
  }
  chars_ = chars_.Add(id, CharInfo{false, cdel->chr, cdel->next, cdel->prev,
                                   cdel->after, cdel->before, AVL<ID>()});
}

void AnnotatedString::IntegrateDecl(ID id, const Attribute& decl) {
  attributes_ = attributes_.Add(id, decl);
}

void AnnotatedString::IntegrateDelDecl(ID id) {
  attributes_ = attributes_.Remove(id);
}

void AnnotatedString::IntegrateMark(ID id, const Annotation& annotation) {
  annotations_ = annotations_.Add(id, annotation);
  ID loc = annotation.begin();
  while (loc != annotation.end()) {
    const CharInfo* ci = chars_.Lookup(loc);
    assert(ci);
    if (ci->visible) {
      chars_.Add(loc, CharInfo{ci->visible, ci->chr, ci->next, ci->prev,
                               ci->after, ci->before, ci->annotations.Add(id)});
    }
    loc = ci->next;
  }
}

void AnnotatedString::IntegrateDelMark(ID id) {
  const Annotation* ann = annotations_.Lookup(id);
  if (!ann) return;
  ID loc = ann->begin();
  while (loc != ann->end()) {
    const CharInfo* ci = chars_.Lookup(loc);
    assert(ci);
    if (ci->visible) {
      chars_.Add(loc,
                 CharInfo{ci->visible, ci->chr, ci->next, ci->prev, ci->after,
                          ci->before, ci->annotations.Remove(id)});
    }
    loc = ci->next;
  }
  annotations_ = annotations_.Remove(id);
}

std::string AnnotatedString::Render() const {
  std::string r;
  ID loc = Begin();
  while (loc != End()) {
    const CharInfo* ci = chars_.Lookup(loc);
    if (ci->visible) {
      r += ci->chr;
    }
    loc = ci->next;
  }
  return r;
}

ID AnnotationEditor::AttrID(const Attribute& attr) {
  std::string ser;
  if (!attr.SerializeToString(&ser)) abort();
  auto it = new_attr2id_.find(ser);
  if (it != new_attr2id_.end()) return it->second;
  it = last_attr2id_.find(ser);
  if (it != last_attr2id_.end()) {
    ID id = it->second;
    new_attr2id_.insert(*it);
    last_attr2id_.erase(it);
    return id;
  }
  ID id = AnnotatedString::MakeDecl(commands_, site_, attr);
  new_attr2id_.emplace(std::make_pair(std::move(ser), id));
  return id;
}

ID AnnotationEditor::Mark(ID beg, ID end, ID attr) {
  Annotation a;
  a.set_begin(beg.id);
  a.set_end(end.id);
  a.set_attribute(attr.id);
  return Mark(a);
}

ID AnnotationEditor::Mark(const Annotation& ann) {
  std::string ser;
  if (!ann.SerializeToString(&ser)) abort();
  auto it = new_ann2id_.find(ser);
  if (it != new_ann2id_.end()) return it->second;
  it = last_ann2id_.find(ser);
  if (it != last_ann2id_.end()) {
    ID id = it->second;
    new_ann2id_.insert(*it);
    last_ann2id_.erase(it);
    return id;
  }
  ID id = AnnotatedString::MakeMark(commands_, site_, ann);
  new_ann2id_.emplace(std::move(ser), id);
  return id;
}

void AnnotationEditor::EndEdit() {
  for (const auto& a : last_ann2id_) {
    AnnotatedString::MakeDelMark(commands_, a.second);
  }
  for (const auto& a : last_attr2id_) {
    AnnotatedString::MakeDelDecl(commands_, a.second);
  }
  commands_ = nullptr;
}
