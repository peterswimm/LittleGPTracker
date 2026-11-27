#include "FxPrinter.h"
#include "Services/Audio/Audio.h"

// Quotes one argument for the shell that system() runs, so a file name can't
// add shell syntax. Returns false for a name that can't be quoted safely.
static bool shellQuote(const std::string &arg, std::string &out) {
#ifdef _WIN32
    // cmd.exe: double quotes, inside which only " and %VAR% are special
    if (arg.find_first_of("\"%") != std::string::npos)
        return false;
    out += "\"" + arg + "\"";
#else
    // POSIX sh: nothing is special inside single quotes, so a ' is written
    // as '\'' (close the quotes, an escaped quote, reopen them)
    out += "'";
    for (size_t i = 0; i < arg.size(); i++)
        out += arg[i] == '\'' ? std::string("'\\''") : std::string(1, arg[i]);
    out += "'";
#endif
    out += " ";
    return true;
}

FxPrinter::FxPrinter(ViewData *viewData)
    : samples_dir("project:samples"), impulse_dir("root:samplelib"),
      viewData_(viewData) {
    int curInstr = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    instrument_ =
        static_cast<SampleInstrument *>(bank->GetInstrument(curInstr));
    notificationResult_ = "";
    // Assume ffmpeg exists but swap for local ffmpig if it doesn't
    ffmpeg_ = "ffmpeg";
    Path pigPath("bin:ffmpig");
    Path ffmpigPath(pigPath.GetPath().c_str());
    if (ffmpigPath.Exists())
        ffmpeg_ = pigPath.GetPath();
}

void FxPrinter::setParams() {
    irPad_ = instrument_->FindVariable(SIP_IR_PAD)->GetInt();
    irWet_ = instrument_->FindVariable(SIP_IR_WET)->GetInt();
}

void FxPrinter::setPaths() {
    fi_ = std::string(instrument_->GetFileName());

    foWav_ = fi_.substr(0, fi_.find_last_of('.')) + "_.wav";
    fo_ = samples_dir.GetPath() + '/' + foWav_;

    ir_ = impulse_dir.GetPath() + "/IR-s/";
    ir_ += std::string(instrument_->FindVariable(SIP_PRINTFX)->GetString());
}

std::string FxPrinter::parseCommand() {
    std::string command;
    float padDur = static_cast<float>(irPad_) / 1000;
    float smplLength = static_cast<float>(instrument_->GetSampleSize()) /
                       Audio::GetInstance()->GetSampleRate();

    std::ostringstream cm2, cm3, cm4;
    // bug in ffmpeg version 4.4.2
    // requires pad_dur to be over 0 or output will be infinitely long
    if (irPad_ > 0) {
        cm2 << "[0:a]apad=pad_dur=" << padDur << "[pad];[pad]";
    } else {
        cm2 << "[0:a]";
    }
    cm3 << "[1:a]afir=dry=10:wet=10[reverb];"
        << "[0] [reverb] amix=inputs=2:weights=100 " << irWet_ << ",volume=1.5";
    if (irPad_ > 0) {
        cm4 << ",afade=out:st=" << smplLength << ":d=" << padDur;
    } else {
        cm4 << "";
    }
    // every argument is quoted: file names come from the SD card
    bool ok = shellQuote(ffmpeg_, command);
    command += "-y -i ";
    ok = ok && shellQuote(samples_dir.GetPath() + "/" + fi_, command);
    command += "-i ";
    ok = ok && shellQuote(ir_, command);
    command += "-filter_complex ";
    ok = ok && shellQuote(cm2.str() + cm3.str() + cm4.str(), command);
    ok = ok && shellQuote(fo_, command);
    if (!ok)
        return "";
#ifdef _WIN32
    // cmd /c drops the first and last quote of a line that starts with one
    command = "\"" + command + "\"";
#endif
    return command;
}

char *FxPrinter::GetNotification() { return notificationResult_; }

bool FxPrinter::Run() {
    setParams();
    setPaths();
    // Are we overwriting an already imported sample?
    bool imported = SamplePool::GetInstance()->IsImported(foWav_);
    std::string cmd = parseCommand();
    Trace::Log("Processed", "%s", cmd.c_str());
    if (!cmd.empty() && system(cmd.c_str()) == 0) {
        int newIndex = SamplePool::GetInstance()->Reassign(foWav_, imported);
        instrument_->AssignSample(newIndex);
        notificationResult_ = "OK!";
        return true;
    } else {
        Trace::Log("PRINTFX", "Failed");
        notificationResult_ = "Failed, check lgpt.log";
        return false;
    }
}
