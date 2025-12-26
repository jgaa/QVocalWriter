#include "RewriteStyleModel.h"

namespace {

// Prompts are templates with QString::arg placeholders:
//   %1 = extra constraints (QString), or social media type for that prompt


static constexpr auto prompts = std::to_array<std::string_view>({
    // 0) Blog post
    R"(You are an expert editor. Convert the raw speech-to-text transcription into a polished BLOG POST.

Hard rules:
- Do not invent facts. If something is unclear, either omit it or mark it as [unclear].
- Keep the author's intent and voice, but remove filler, repetitions, and false starts.
- Fix grammar, punctuation, and structure.

Output requirements:
- Use Markdown.
- Start with a strong title (H1).
- Include an engaging intro, clear sections with headings, and a short conclusion.
- Add bullet lists where helpful.
- If appropriate, add a short “Key takeaways” section.
- End with a short summary

Extra constraints (if any): %1
)",

    // 1) Email
    R"(You are an assistant who turns a raw transcription into a clear EMAIL.

Hard rules:
- Do not add facts not present in the transcription.
- Remove filler words, repetitions, and tangents.
- Keep tone professional, friendly, and direct.
- Do not provide alternatives. Only one email as output.

Output requirements:
- Provide: Subject line + email body.
- Use short paragraphs and, if useful, bullets.
- If the transcription implies a request, include a clear call-to-action and next steps.

Extra constraints (if any): %1
)",

    // 2) Social media posts
    R"(You are a social media editor. Create a SOCIAL MEDIA POST from the transcription.

Hard rules:
- Do not invent claims, stats, or events.
- Keep wording safe and non-defamatory.
- Preserve intent; remove fluff.
- Add tags/hashtags if appropriate for the platform.

Output requirements:
- Create a post suited for %1
)",

    // 3) Technical documentation
    R"(You are a technical writer. Turn the transcription into TECHNICAL DOCUMENTATION.

Hard rules:
- Do not invent APIs, commands, numbers, or behaviors.
- If details are missing, add a “TBD / Unknown” note instead of guessing.
- Prefer precise, unambiguous phrasing.

Output requirements (Markdown):
- Overview
- Goals / Non-goals (if implied)
- Concepts / Terminology
- Requirements
- Design (components, data flow)
- Usage / Examples (only if present; otherwise “TBD”)
- Edge cases & Error handling
- Open questions

Extra constraints (if any): %1

)",

    // 4) Meeting notes
    R"(You are a diligent note-taker. Convert the transcription into MEETING NOTES.

Hard rules:
- Do not add attendees, decisions, or dates that are not present.
- If names are unclear, keep them as spoken or mark [unknown speaker].

Output requirements (Markdown):
- Summary (3–6 bullets)
- Decisions (bullets; if none, write “None captured”)
- Action items (who / what / due date if present; otherwise omit due date)
- Discussion notes (grouped by topic)
- Risks / blockers
- Follow-ups / next meeting (if mentioned)

Extra constraints (if any): %1
)",

    // 5) Structured plans from inspired rambling
    R"(You are a pragmatic organizer. Turn this “inspired rambling” transcription into a STRUCTURED PLAN.

Hard rules:
- Do not invent facts; only reorganize and clarify what’s there.
- If something is aspirational or vague, keep it as an assumption or a question.

Output requirements (Markdown):
- One-sentence mission
- Objectives (3–7)
- Scope (in / out)
- Assumptions
- Plan:
  - Phase 1 (next 1–2 weeks)
  - Phase 2 (next 1–2 months)
  - Phase 3 (later)
- Milestones
- Risks & mitigations
- Immediate next actions (5–10 bullets)

Extra constraints (if any): %1
)",

    // 6) Creative writing
    R"(You are a creative writing editor. Transform the transcription into a piece of CREATIVE WRITING.

Hard rules:
- Stay faithful to the themes and content of the transcription.
- You may improve imagery and flow, but do not introduce major new plot facts or real-world claims not implied.
- Keep names/places consistent with what’s said; if unclear, choose neutral placeholders.

Output requirements:
- Provide 3 options:
  A) Short story (800–1400 words)
  B) Poem (free verse, 20–40 lines)
  C) Script scene (1–3 pages, dialogue + stage directions)
- Each option should share the same core theme.

Extra constraints (if any): %1
)",

    // 7) Conservative clean-up (medical/legal memos)
    R"(You are an ultra-conservative editor. Clean up the transcription into a FORMAL MEMO suitable for medical/legal contexts.

Hard rules (very important):
- Do NOT add, infer, or “smooth over” missing facts.
- Do NOT provide medical or legal advice.
- Preserve meaning; remove only disfluencies (uh, um), repetitions, and obvious transcription artifacts.
- If a statement is ambiguous, keep it but mark [ambiguous] or [unclear].
- Keep dates, numbers, and proper nouns exactly as spoken; if uncertain, mark [unclear].

Output requirements (plain, structured):
- Header: “Memo”
- Sections:
  - Purpose
  - Facts as stated (bullets)
  - Statements attributed to speaker(s) (if multiple)
  - Uncertainties / ambiguities (bullets)
  - Items requiring verification (bullets)
- No persuasive tone. No speculation.

Extra constraints (if any): %1
)",
    // X) Rant cleanup (retain emotion, improve clarity)
    R"(You are an editor tasked with refining a raw RANT transcript.

Primary goal:
- Preserve the emotional force, frustration, and strong opinions of the speaker.
- Do NOT neutralize, soften, or “professionalize” the tone.
- Do NOT add new arguments or facts.

Editing rules:
- Remove filler words, repetition, and transcription artifacts.
- Improve sentence flow and paragraph structure.
- Clarify arguments where they are muddled, without changing intent.
- If something is unclear or contradictory, keep it but mark it [unclear].

Output requirements (Markdown):
- Start with a strong, expressive title (H1) that reflects the core frustration.
- Use short paragraphs for readability.
- Use bullet points for lists of grievances or arguments where appropriate.
- Use emphasis (**bold**, *italics*) sparingly to reflect emotional peaks.
- Do NOT moralize, summarize, or “calm down” the rant.

Extra constraints (if any): %1
)",
R"(You are a transcription editor. Your task is to CLEAN UP the text, not rewrite it.

Absolute rules:
- Do NOT change meaning, tone, intent, or structure.
- Do NOT summarize, rephrase, or reorganize.
- Do NOT add titles, headings, or conclusions.
- Do NOT infer missing words or facts.

Allowed edits ONLY:
- Remove filler words (um, uh, you know) when they clearly add no meaning.
- Fix obvious speech-to-text errors.
- Fix grammar, punctuation, and capitalization.
- Split run-on text into sentences and paragraphs ONLY where clearly implied by speech.
- Preserve informal language, fragments, and repetitions if they carry meaning.

Uncertainty handling:
- If a word or phrase is unclear, keep it and mark [unclear].
- If multiple interpretations exist, keep the original phrasing.

Output requirements:
- Plain text only (no Markdown, no formatting).
- Same order as the original transcript.
- No commentary, no explanation.

Extra constraints (if any): %1
)"
});

} // anonymous namespace


RewriteStyleModel::RewriteStyleModel(const QString &settingsKey, QObject *parent)
    : QAbstractListModel(parent), settings_key_{settingsKey}
{
    items_.emplace_back(Kind::None, tr("None"), -1);
    items_.emplace_back(Kind::Blog, tr("Blog post"), 0);
    items_.emplace_back(Kind::Email, tr("Email"), 1);
    items_.emplace_back(Kind::SocialMedia, tr("Social media posts"), 2);
    items_.emplace_back(Kind::TechnicalDoc, tr("Technical documentation"), 3);
    items_.emplace_back(Kind::MeetingNotes, tr("Meeting notes"), 4);
    items_.emplace_back(Kind::Plan, tr("Structured plan from rambling"), 5);
    items_.emplace_back(Kind::Creative, tr("Creative writing (story / poem / script)"), 6);
    items_.emplace_back(Kind::Conservative, tr("Conservative cleanup (medical/legal memo)"), 7);
    items_.emplace_back(Kind::Rant, tr("Rant cleanup (keep tone)"), 8);
    items_.emplace_back(Kind::Clean, tr("Clean transcript (no rewriting)"), 9);

    // Sort by name, but none is always first
    std::sort(items_.begin() + 1, items_.end(), [](const Item& a, const Item& b) {
        if (a.prompt_index == -1) {
            return true;
        }
        if (b.prompt_index == -1) {
            return false;
        }
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    loadSelectedFromSettings();
}


int RewriteStyleModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(items_.size());
}

QVariant RewriteStyleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= rowCount()) return {};

    const auto& it = items_[static_cast<size_t>(row)];
    switch (static_cast<Roles>(role)) {
    case Roles::Name:     return it.name;
    case Roles::Kind:     return static_cast<int>(it.kind);
    case Roles::HasPrompt:return it.prompt_index >= 0;
    }
    return {};
}

QHash<int, QByteArray> RewriteStyleModel::roleNames() const
{
    QHash<int, QByteArray> r;
    r[int(Roles::Name)] = "name";
    r[int(Roles::Kind)] = "kind";
    r[int(Roles::HasPrompt)] = "hasPrompt";
    return r;
}

int RewriteStyleModel::selected() const noexcept { return selected_; }

QString RewriteStyleModel::extra() const {
    switch (selectedKind()) {
    case Kind::SocialMedia:
        return socialMediaType();
    default:
        break;
    }
    return {};
}

void RewriteStyleModel::setSelected(int index)
{
    if (index < 0 || index >= rowCount()) index = 0; // clamp to "none"
    if (selected_ == index) return;

    selected_ = index;
    saveSelectedToSettings();

    emit selectedChanged();
}

RewriteStyleModel::Kind RewriteStyleModel::selectedKind() const
{
    const auto& it = items_[static_cast<size_t>(clampedSelected_())];
    return it.kind;
}

QString RewriteStyleModel::selectedName() const
{
    const auto& it = items_[static_cast<size_t>(clampedSelected_())];
    return it.name;
}

QString RewriteStyleModel::socialMediaType() const {
    if (social_media_type_.isEmpty()) {
        return "Generic";
    }
    return social_media_type_;
}

QString RewriteStyleModel::settingsKey() const {
    return settings_key_;
}


QString RewriteStyleModel::makePrompt() const
{
    const auto& it = items_[static_cast<size_t>(clampedSelected_())];
    if (it.prompt_index < 0) return {};

    const auto tmpl = prompts[static_cast<size_t>(it.prompt_index)];
    const QString qtmpl = QString::fromUtf8(tmpl.data(), int(tmpl.size()));
    return qtmpl.arg(extra());
}

int RewriteStyleModel::clampedSelected_() const noexcept
{
    if (selected_ < 0) return 0;
    if (selected_ >= static_cast<int>(items_.size())) return 0;
    return selected_;
}

void RewriteStyleModel::loadSelectedFromSettings()
{
    if (settings_key_.isEmpty()) return;
    QSettings s;
    const int idx = s.value(settings_key_, 0).toInt();
    const int clamped = (idx >= 0 && idx < rowCount()) ? idx : 0;
    if (selected_ != clamped) {
        selected_ = clamped;
        emit selectedChanged();
    }
}

void RewriteStyleModel::saveSelectedToSettings() const
{
    if (settings_key_.isEmpty()) return;
    QSettings s;
    s.setValue(settings_key_, clampedSelected_());
}
