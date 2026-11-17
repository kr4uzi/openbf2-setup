#include "window.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <deque>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

using namespace setup;
namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
using Kind = Event::Kind;

class Shell : public QWidget {
public:
	std::function<void()> closing;
	void closeEvent(QCloseEvent* event) override { event->ignore(); closing(); }
};

class PageStack : public QStackedWidget {
public:
	QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize{}; }
	QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize{}; }
};

QIcon commandLockIcon(bool open, QWidget* widget) {
	// A small vector fallback also works in the portable bundle without an icon theme.
	auto scale = widget->devicePixelRatioF();
	QPixmap pixmap(qRound(20 * scale), qRound(20 * scale));
	pixmap.setDevicePixelRatio(scale); pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(widget->palette().color(QPalette::Text), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawRoundedRect(QRectF(4, 9, 11, 8), 1.5, 1.5);
	QPainterPath shackle; shackle.moveTo(7, 9); shackle.lineTo(7, 6);
	shackle.cubicTo(7, 1, 14, 1, 14, 6); if (!open) shackle.lineTo(14, 9);
	painter.drawPath(shackle); painter.drawLine(QPointF(9.5, 12), QPointF(9.5, 14));
	painter.end();
	return QIcon::fromTheme(open ? "object-unlocked" : "object-locked", QIcon(pixmap));
}

class DiscDialog : public QFileDialog {
public:
	explicit DiscDialog(QWidget* parent) : QFileDialog(parent) {
		// QFileDialog has no mixed file/directory mode. Keep normal double-click
		// navigation, but let Choose accept either kind of existing selection.
		setOption(DontUseNativeDialog);
		setWindowTitle("Choose Battlefield 2 DVD or ISO");
		setFileMode(ExistingFile);
		setNameFilter("Disc images (*.iso *.ISO);;All files (*)");
		setLabelText(FileName, "DVD folder or ISO:");
		setLabelText(Accept, "Choose");
	}
protected:
	void accept() override {
		const auto paths = selectedFiles();
		if (paths.size() == 1 && QFileInfo(paths.first()).isDir())
			QDialog::accept();
		else
			QFileDialog::accept();
	}
};

class FileWell : public QGroupBox {
	QLabel* path;
	QPushButton* file;
	bool included = false;
public:
	std::function<void(const std::filesystem::path&)> chosen;
	QProgressBar* progress;
	QLabel* status;
	explicit FileWell(const QString& title, bool disc) : QGroupBox(title) {
		setAcceptDrops(true);
		auto layout = new QVBoxLayout(this);
		path = new QLabel(disc ? "Choose an ISO file or DVD folder, or drop it here" : "Choose a file or drop it here");
		path->setWordWrap(true);
		path->setTextFormat(Qt::PlainText);
		layout->addWidget(path);
		auto row = new QHBoxLayout;
		file = new QPushButton(disc ? "Choose DVD / ISO…" : "Choose patch…");
		row->addWidget(file);
		if (disc && (qEnvironmentVariableIsSet("FLATPAK_ID") || QFileInfo::exists("/.flatpak-info"))) {
			// The portal requires a file OR directory request. Preserve its access
			// grants in Flatpak, with both choices behind the same media button.
			auto menu = new QMenu(file);
			menu->addAction("ISO file…", this, [this] {
				auto selected = QFileDialog::getOpenFileName(this, "Choose Battlefield 2 ISO", {}, "Disc images (*.iso *.ISO);;All files (*)");
				if (!selected.isEmpty()) select(selected);
			});
			menu->addAction("DVD folder…", this, [this] {
				auto selected = QFileDialog::getExistingDirectory(this, "Choose mounted DVD folder");
				if (!selected.isEmpty()) select(selected);
			});
			file->setMenu(menu);
		} else if (disc) {
			connect(file, &QPushButton::clicked, this, [this] {
				DiscDialog dialog(this);
				if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) select(dialog.selectedFiles().first());
			});
		} else {
			connect(file, &QPushButton::clicked, this, [this] {
				auto selected = QFileDialog::getOpenFileName(this, "Choose official patch", {}, "Patch files (*.exe);;All files (*)");
				if (!selected.isEmpty()) select(selected);
			});
		}
		layout->addLayout(row);
		status = new QLabel;
		status->setTextFormat(Qt::PlainText);
		layout->addWidget(status);
		progress = new QProgressBar;
		progress->setRange(0, 1000);
		progress->hide();
		layout->addWidget(progress);
	}
	void automatic(bool value) {
		if (included != value) { path->setText("Choose a file or drop it here"); status->clear(); progress->hide(); }
		included = value;
		file->setVisible(!value); file->setEnabled(!value); setAcceptDrops(!value);
		if (value) path->setText("Included on disc — Complete Edition");
	}
	bool automatic() const { return included; }
	void select(const QString& selected) {
		if (!isEnabled() || included) return;
		path->setText(selected);
		chosen(std::filesystem::path{selected.toStdString()});
	}
	void dragEnterEvent(QDragEnterEvent* event) override {
		if (isEnabled() && !included && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile())
			event->acceptProposedAction();
	}
	void dropEvent(QDropEvent* event) override {
		if (isEnabled() && !included && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile()) {
			select(event->mimeData()->urls().first().toLocalFile());
			event->acceptProposedAction();
		}
	}
};

class KeyField : public QLineEdit {
public:
	std::function<void(QString)> paste;
	std::function<void()> previous;
	void keyPressEvent(QKeyEvent* event) override {
		if (event->matches(QKeySequence::Paste)) { paste(QApplication::clipboard()->text()); return; }
		if (event->key() == Qt::Key_Backspace && text().isEmpty()) { previous(); return; }
		QLineEdit::keyPressEvent(event);
	}
};

class KeyRow : public QWidget {
public:
	std::array<KeyField*, 5> fields;
	QLabel* status;
	std::function<void()> changed;
	explicit KeyRow(const QString& title) {
		auto outer = new QVBoxLayout(this);
		outer->setContentsMargins(0, 0, 0, 0);
		outer->addWidget(new QLabel(title));
		auto row = new QHBoxLayout;
		for (size_t i = 0; i < fields.size(); ++i) {
			auto field = fields[i] = new KeyField;
			field->setAccessibleName(title + QString(" group %1").arg(i + 1));
			field->setMaxLength(64); // full-key paste from the context menu is normalized below
			row->addWidget(field);
			field->paste = [this, i](QString value) { set(value, i); };
			field->previous = [this, i] { if (i) { fields[i - 1]->setFocus(); fields[i - 1]->backspace(); } };
			connect(field, &QLineEdit::textEdited, this, [this, i](const QString& value) { set(value, i); });
		}
		outer->addLayout(row);
		status = new QLabel;
		outer->addWidget(status);
	}
	void set(QString value, size_t start = 0) {
		QString normalized;
		for (auto c : value.toUpper()) if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) normalized += c;
		if (normalized.size() > 4) {
			if (normalized.size() >= 20) start = 0;
			for (size_t i = start; i < fields.size(); ++i) fields[i]->setText(normalized.mid((i - start) * 4, 4));
			fields[std::min(size_t(4), start + size_t(normalized.size() / 4))]->setFocus();
		} else {
			fields[start]->setText(normalized);
			if (normalized.size() == 4 && start < 4) fields[start + 1]->setFocus();
		}
		if (changed) changed();
	}
	std::string value() const { QString value; for (auto field : fields) value += field->text(); return value.toStdString(); }
	void clear() { for (auto field : fields) field->clear(); }
};
}

struct Window::Impl {
	boost::asio::io_context& context;
	boost::asio::steady_timer wake;
	std::deque<Event> events;
	Shell shell;
	QTimer bridge;
	QStackedWidget* pages;
	QVBoxLayout* outer;
	QWidget* windowSize;
	QToolButton* unlock;
	QPushButton *next, *back, *microphone, *skip, *voice, *verify, *modsFolder;
	QLabel *heading, *installStatus, *micStatus, *profileNote, *launcherNote, *optionHint;
	QProgressBar* meter;
	std::array<QProgressBar*, 4> installProgress;
	FileWell *disc, *patch141, *patch150;
	KeyRow *baseKey, *sfKey;
	QCheckBox *specialForces, *movies, *fullscreen, *always, *boost;
	QComboBox* mods;
	QLineEdit *server, *password, *player, *command;
	QSpinBox *width, *height, *port;
	QSlider *volume, *threshold;
	double playback = 1;
	Page shown = Page::welcome;
	bool hidden = false, loading = false, busy = false, polling = false, deluxe = false, cancelling = false;

	explicit Impl(boost::asio::io_context& io) : context(io), wake(io) {
		shell.setWindowTitle("OpenBF2");
		shell.setMinimumSize(700, 640);
		shell.resize(760, 720);
		shell.closing = [this] { send(Kind::close); };
		outer = new QVBoxLayout(&shell);
		outer->setContentsMargins(28, 24, 28, 24);
		heading = new QLabel;
		auto font = heading->font(); font.setPointSize(22); font.setBold(true); heading->setFont(font);
		outer->addWidget(heading);
		pages = new PageStack;
		outer->addWidget(pages, 1);
		auto navigation = new QHBoxLayout;
		back = new QPushButton("Back");
		next = new QPushButton("Continue"); next->setDefault(true);
		navigation->addWidget(back);
		voice = button(navigation, "Voice Setup…", Kind::voice_setup);
		verify = button(navigation, "Verify Game Data…", Kind::verify);
		voice->setObjectName("voiceSetupButton"); verify->setObjectName("verifyGameDataButton");
		voice->hide(); verify->hide();
		navigation->addStretch(); navigation->addWidget(next); outer->addLayout(navigation);
		next->setMinimumWidth(100);
		QObject::connect(back, &QPushButton::clicked, &shell, [this] { send(Kind::back); });
		QObject::connect(next, &QPushButton::clicked, &shell, [this] { send(Kind::next); });

		auto welcome = page();
		label(welcome, "Install Battlefield 2 from your DVD or ISO and the official 1.41 and 1.5 patches. You will need your CD key.");
		label(welcome, "Game data, profiles and mods stay in your user data folder when you replace this application. A paused installation can be resumed.");
		welcome->addStretch();
		auto discs = page();
		label(discs, "Choose a Battlefield 2, Deluxe or Complete Edition DVD folder or ISO image.");
		disc = well(discs, "Battlefield 2 DVD / ISO", Kind::disc_chosen, true); discs->addStretch();
		auto patches = page();
		label(patches, "Setup verifies both patches. Complete Edition supplies 1.41 from its disc; choose 1.5 below.");
		patch141 = well(patches, "Patch 1.41", Kind::patch141_chosen);
		patch150 = well(patches, "Patch 1.5", Kind::patch150_chosen); patches->addStretch();
		auto keys = page();
		label(keys, "Enter the key from your copy. You can paste a complete key into any group.");
		baseKey = new KeyRow("Battlefield 2 CD key"); keys->addWidget(baseKey);
		specialForces = new QCheckBox("Install Special Forces"); specialForces->setChecked(true); keys->addWidget(specialForces);
		sfKey = new KeyRow("Special Forces CD key"); keys->addWidget(sfKey); keys->addStretch();
		baseKey->changed = sfKey->changed = [this] { send(Kind::keys_edited); };
		QObject::connect(specialForces, &QCheckBox::toggled, &shell, [this](bool checked) { sfKey->setEnabled(checked); send(Kind::keys_edited); });
		auto install = page();
		installStatus = label(install, "Checking your media…");
		installStatus->setObjectName("installStatus");
		const std::array<const char*, 4> steps{"DVD files", "Patch 1.0 → 1.41", "Patch 1.41 → 1.50", "Verifying game data"};
		for (size_t row = 0; row < steps.size(); ++row) {
			auto group = new QGroupBox(steps[row]);
			auto layout = new QVBoxLayout(group);
			auto bar = installProgress[row] = new QProgressBar;
			bar->setObjectName(QString("installProgress%1").arg(row));
			bar->setAccessibleName(steps[row]);
			layout->addWidget(bar); install->addWidget(group);
		}
		label(install, "Your original files are only read. Nothing is downloaded.");
		install->addStretch();
		auto audio = page();
		label(audio, "BF2 BattleCommo · Voice chat\nSpeak into your microphone to test its level. Save these defaults for new profiles, or skip this step.");
		profileNote = label(audio, "");
		micStatus = label(audio, "");
		microphone = new QPushButton("Retry microphone"); audio->addWidget(microphone);
		QObject::connect(microphone, &QPushButton::clicked, &shell, [this] { send(Kind::microphone); });
		meter = new QProgressBar; meter->setRange(0, 1000); meter->setTextVisible(false); audio->addWidget(meter);
		volume = slider(audio, "Capture volume"); threshold = slider(audio, "Activation threshold");
		boost = new QCheckBox("Microphone boost"); audio->addWidget(boost);
		skip = new QPushButton("Skip voice setup"); audio->addWidget(skip);
		QObject::connect(skip, &QPushButton::clicked, &shell, [this] { send(Kind::voice_skip); }); audio->addStretch();
		auto launcher = page();
		launcher->setContentsMargins(0, 12, 0, 0); launcher->setSpacing(12);
		auto preferences = new QGridLayout; preferences->setHorizontalSpacing(20); preferences->setVerticalSpacing(10);
		preferences->setColumnStretch(0, 1); preferences->setColumnStretch(1, 1);
		always = new QCheckBox("Open this screen every time"); always->setObjectName("alwaysShowLauncher");
		preferences->addWidget(always, 0, 0);
		optionHint = new QLabel("Tip: Open with --launcher to return here"); optionHint->setObjectName("launcherHint");
		auto smallFont = optionHint->font(); smallFont.setPointSizeF(smallFont.pointSizeF() - 1); optionHint->setFont(smallFont);
		optionHint->setForegroundRole(QPalette::PlaceholderText); preferences->addWidget(optionHint, 0, 1);
		movies = new QCheckBox("Skip the intro movies"); movies->setObjectName("skipMovies"); preferences->addWidget(movies, 1, 0);
		fullscreen = new QCheckBox("Fullscreen"); fullscreen->setObjectName("fullscreen"); preferences->addWidget(fullscreen, 1, 1);
		windowSize = new QWidget; windowSize->setObjectName("windowSize");
		auto retained = windowSize->sizePolicy(); retained.setRetainSizeWhenHidden(true); windowSize->setSizePolicy(retained);
		auto size = new QHBoxLayout(windowSize); size->setContentsMargins(0, 0, 0, 0);
		width = spin(0, 16384); height = spin(0, 16384); width->setSpecialValueText("Default"); height->setSpecialValueText("Default");
		width->setAccessibleName("Window width"); height->setAccessibleName("Window height");
		size->addWidget(new QLabel("Window size")); size->addWidget(width); size->addWidget(new QLabel("×")); size->addWidget(height);
		preferences->addWidget(windowSize, 2, 1); launcher->addLayout(preferences);
		auto form = new QGridLayout; form->setHorizontalSpacing(12); form->setVerticalSpacing(10); form->setColumnStretch(1, 1);
		mods = new QComboBox; mods->setAccessibleName("Mod"); mods->setObjectName("launcherMod");
		modsFolder = new QPushButton("Open Mods Folder");
		QObject::connect(modsFolder, &QPushButton::clicked, &shell, [this] { send(Kind::mods_folder); });
		form->addWidget(new QLabel("Mod"), 0, 0); form->addWidget(mods, 0, 1); form->addWidget(modsFolder, 0, 2, 1, 2);
		server = new QLineEdit; server->setAccessibleName("Server address"); server->setPlaceholderText("address, empty for the menu");
		port = spin(1, 65535); port->setAccessibleName("Server port"); port->setMaximumWidth(90);
		form->addWidget(new QLabel("Join server"), 1, 0); form->addWidget(server, 1, 1);
		form->addWidget(new QLabel("Port"), 1, 2); form->addWidget(port, 1, 3, Qt::AlignLeft);
		player = new QLineEdit; player->setAccessibleName("Player name"); player->setPlaceholderText("the game’s own");
		password = new QLineEdit; password->setAccessibleName("Server password"); password->setEchoMode(QLineEdit::Password); password->setPlaceholderText("none");
		password->setMaximumWidth(140);
		form->addWidget(new QLabel("Player name"), 2, 0); form->addWidget(player, 2, 1);
		form->addWidget(new QLabel("Password"), 2, 2); form->addWidget(password, 2, 3); launcher->addLayout(form);
		auto arguments = new QHBoxLayout;
		auto argumentLabel = new QLabel("Arguments to bf2_client"); argumentLabel->setFont(smallFont); argumentLabel->setForegroundRole(QPalette::PlaceholderText);
		arguments->addWidget(argumentLabel); arguments->addStretch();
		launcherNote = new QLabel; launcherNote->setTextFormat(Qt::PlainText); launcherNote->setWordWrap(true); launcherNote->setFont(smallFont);
		launcherNote->setAlignment(Qt::AlignRight | Qt::AlignVCenter); arguments->addWidget(launcherNote, 1); launcher->addLayout(arguments);
		auto commandRow = new QHBoxLayout; commandRow->setSpacing(6);
		unlock = new QToolButton; unlock->setObjectName("commandLineLock"); unlock->setCheckable(true); unlock->setAutoRaise(true);
		unlock->setIconSize(QSize(20, 20)); commandRow->addWidget(unlock);
		command = new QLineEdit; command->setObjectName("commandLine"); command->setAccessibleName("Command line"); command->setReadOnly(true);
		auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont); fixedFont.setPointSizeF(smallFont.pointSizeF()); command->setFont(fixedFont);
		commandRow->addWidget(command, 1); launcher->addLayout(commandRow); launcher->addStretch();
		QWidget::setTabOrder(always, movies); QWidget::setTabOrder(movies, fullscreen); QWidget::setTabOrder(fullscreen, width);
		QWidget::setTabOrder(width, height); QWidget::setTabOrder(height, mods); QWidget::setTabOrder(mods, modsFolder);
		QWidget::setTabOrder(modsFolder, server); QWidget::setTabOrder(server, port); QWidget::setTabOrder(port, player);
		QWidget::setTabOrder(player, password); QWidget::setTabOrder(password, unlock); QWidget::setTabOrder(unlock, command);
		QWidget::setTabOrder(command, voice); QWidget::setTabOrder(voice, verify); QWidget::setTabOrder(verify, next);
		for (auto control : {movies, fullscreen, always}) QObject::connect(control, &QCheckBox::toggled, &shell, [this] { edited(); });
		QObject::connect(unlock, &QToolButton::toggled, &shell, [this] { edited(); });
		for (auto control : {width, height, port}) QObject::connect(control, &QSpinBox::valueChanged, &shell, [this] { edited(); });
		for (auto control : {server, password, player, command}) QObject::connect(control, &QLineEdit::textEdited, &shell, [this] { edited(); });
		QObject::connect(mods, &QComboBox::currentIndexChanged, &shell, [this] { edited(); });
		QObject::connect(qApp, &QApplication::applicationStateChanged, &shell, [this](Qt::ApplicationState state) {
			if (state == Qt::ApplicationActive && shown == Page::launcher && !busy) send(Kind::activated);
		});
		QObject::connect(&bridge, &QTimer::timeout, &shell, [this] {
			// A native dialog or error alert can run a nested Qt loop. Never restart or
			// recursively dispatch the Asio context while a previous slice still owns it.
			if (polling) return;
			polling = true;
			struct Dispatch { bool& active; ~Dispatch() { active = false; } } dispatch{polling};
			context.restart();
			// Bound each dispatch slice so progress floods cannot starve Qt input/painting.
			for (int count = 0; count < 128 && context.poll_one(); ++count) {}
		});
	}
	QVBoxLayout* page() { auto widget = new QWidget; auto layout = new QVBoxLayout(widget); pages->addWidget(widget); return layout; }
	QLabel* label(QVBoxLayout* layout, const QString& value) {
		auto label = new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); layout->addWidget(label); return label;
	}
	FileWell* well(QVBoxLayout* layout, const QString& title, Kind kind, bool disc = false) {
		auto well = new FileWell(title, disc); layout->addWidget(well);
		well->setObjectName(kind == Kind::disc_chosen ? "discWell" : kind == Kind::patch141_chosen ? "patch141Well" : "patch150Well");
		well->chosen = [this, kind](const std::filesystem::path& file) { send(kind, file); }; return well;
	}
	QSlider* slider(QVBoxLayout* layout, const QString& name) {
		label(layout, name); auto slider = new QSlider(Qt::Horizontal); slider->setAccessibleName(name); slider->setRange(0, 1000); layout->addWidget(slider); return slider;
	}
	QSpinBox* spin(int low, int high) { auto spin = new QSpinBox; spin->setRange(low, high); return spin; }
	QPushButton* button(QHBoxLayout* layout, const QString& title, Kind kind) {
		auto button = new QPushButton(title); layout->addWidget(button); QObject::connect(button, &QPushButton::clicked, &shell, [this, kind] { send(kind); }); return button;
	}
	void send(Kind kind, std::filesystem::path file = {}) {
		if (loading) return;
		events.push_back({kind, std::move(file)}); wake.cancel_one();
	}
	void edited() {
		command->setReadOnly(!unlock->isChecked());
		unlock->setIcon(commandLockIcon(unlock->isChecked(), unlock));
		unlock->setAccessibleName(unlock->isChecked() ? "Lock the command line" : "Edit the command line");
		unlock->setToolTip(unlock->isChecked() ? "Lock: back to the controls" : "Unlock: write the command line yourself");
		optionHint->setVisible(!always->isChecked());
		windowSize->setVisible(!fullscreen->isChecked());
		for (QWidget* control : std::initializer_list<QWidget*>{mods, movies, fullscreen, server, port, password, player}) control->setEnabled(!unlock->isChecked());
		width->setEnabled(!unlock->isChecked() && !fullscreen->isChecked()); height->setEnabled(width->isEnabled());
		send(Kind::options_edited);
	}
	void show(Page page, const QString& title, const QString& action, bool canBack) {
		bool wasLauncher = shown == Page::launcher, launcher = page == Page::launcher;
		shown = page; busy = false; pages->setEnabled(true); pages->setCurrentIndex(int(page));
		pages->updateGeometry();
		voice->setVisible(launcher); verify->setVisible(launcher); voice->setEnabled(true); verify->setEnabled(true);
		outer->setContentsMargins(launcher ? 40 : 28, launcher ? 38 : 24, launcher ? 40 : 28, 24);
		auto font = heading->font(); font.setPointSize(launcher ? 20 : 22); font.setBold(!launcher); heading->setFont(font);
		shell.setMinimumSize(launcher ? QSize(660, 480) : QSize(700, 640));
		if (launcher != wasLauncher) shell.resize(launcher ? QSize(700, 500) : QSize(760, 720));
		heading->setText(title); next->setText(action); next->setEnabled(true); back->setVisible(canBack); back->setEnabled(true);
		if (!hidden) shell.show();
	}
};

Window::Window(boost::asio::io_context& context) : m_Impl(std::make_unique<Impl>(context)) {}
Window::~Window() = default;
void Window::Run() { m_Impl->bridge.start(5); QApplication::exec(); m_Impl->bridge.stop(); }
void Window::Close() { m_Impl->shell.hide(); QApplication::quit(); }
task<Event> Window::WaitForEvent() {
	while (m_Impl->events.empty()) {
		m_Impl->wake.expires_at(std::chrono::steady_clock::time_point::max());
		boost::system::error_code error;
		co_await m_Impl->wake.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, error));
		if ((co_await boost::asio::this_coro::cancellation_state).cancelled() != boost::asio::cancellation_type::none)
			throw boost::system::system_error(boost::asio::error::operation_aborted);
	}
	auto event = std::move(m_Impl->events.front()); m_Impl->events.pop_front(); co_return event;
}
bool Window::LauncherRequested() const { return QApplication::arguments().contains("--launcher"); }
void Window::ShowWelcome() { m_Impl->show(Page::welcome, "Welcome to OpenBF2", "Continue", false); }
void Window::ShowDisc() { m_Impl->show(Page::disc, "Your Battlefield 2 disc", "Continue", true); }
void Window::ShowPatches(bool included141) { m_Impl->show(Page::patches, "Official patches", "Continue", true); m_Impl->patch141->automatic(included141); }
void Window::ShowKeys(bool deluxe, bool back) {
	m_Impl->deluxe = deluxe; m_Impl->specialForces->setVisible(deluxe); m_Impl->sfKey->setVisible(deluxe);
	m_Impl->show(Page::keys, "Your CD keys", back ? "Install" : "Save", back);
}
void Window::ShowInstall() {
	auto& i = *m_Impl;
	i.cancelling = false;
	i.installStatus->setText("Checking your media…");
	for (auto bar : i.installProgress) { bar->setRange(0, 1000); bar->setValue(0); bar->setFormat("Waiting"); }
	i.show(Page::install, "Installing Battlefield 2", "Cancel", false);
}
void Window::ShowVoice(const assistant::VoiceSettings& settings, bool profiles) {
	m_Impl->playback = settings.playbackVolume; m_Impl->volume->setValue(int(settings.captureVolume * 1000));
	m_Impl->threshold->setValue(int(settings.captureThreshold * 1000)); m_Impl->boost->setChecked(settings.boost);
	m_Impl->show(Page::voice, "Voice setup", "Save", false);
	m_Impl->profileNote->setText(profiles ? "Existing profiles keep their own audio settings. These defaults apply to new profiles." : "");
	m_Impl->meter->setValue(0);
}
void Window::ShowLauncher(const LaunchOptions& options, const std::vector<Mod>& mods, bool present, const std::string& note) {
	auto& i = *m_Impl; i.loading = true;
	i.mods->clear(); for (const auto& mod : mods) i.mods->addItem(text(mod.title), text(mod.directory));
	i.mods->setCurrentIndex(i.mods->findData(text(options.mod))); i.movies->setChecked(options.skip_movies); i.fullscreen->setChecked(options.fullscreen);
	i.width->setValue(options.width); i.height->setValue(options.height); i.port->setValue(options.port);
	i.server->setText(text(options.server)); i.password->setText(text(options.password)); i.player->setText(text(options.player));
	i.always->setChecked(options.always_show); i.unlock->setChecked(!options.override.empty()); i.command->setText(text(options.override));
	i.launcherNote->setText(text(note)); i.launcherNote->setVisible(!note.empty()); i.show(Page::launcher, "Battlefield 2", "Play", false); i.next->setEnabled(present);
	i.edited(); i.loading = false;
}
void Window::SetBusy(const std::string& label) { m_Impl->busy = true; m_Impl->pages->setEnabled(false); m_Impl->voice->setEnabled(false); m_Impl->verify->setEnabled(false); m_Impl->next->setText(text(label)); m_Impl->next->setEnabled(false); m_Impl->back->setEnabled(false); }
void Window::EnableNext(bool enabled) { m_Impl->next->setEnabled(enabled && !m_Impl->busy); }
void Window::SetPatch(PatchSlot slot, PatchStatus status, double fraction) {
	auto well = slot == PatchSlot::Patch141 ? m_Impl->patch141 : m_Impl->patch150;
	well->status->setText(status == PatchStatus::checking ? "Checking…" : status == PatchStatus::verified ? "Verified official patch" : well->automatic() ? "Check failed — go back and check your disc" : "Check failed — choose another file to retry");
	well->progress->setVisible(status == PatchStatus::checking); well->progress->setValue(int(std::clamp(fraction, 0.0, 1.0) * 1000));
}
KeyEntry Window::Keys() const { return {m_Impl->baseKey->value(), m_Impl->sfKey->value(), m_Impl->specialForces->isChecked()}; }
void Window::ClearKeys() { m_Impl->baseKey->clear(); m_Impl->sfKey->clear(); }
void Window::SetKeyStatus(KeyEdition row, KeyStatus status) {
	auto key = row == KeyEdition::BF2 ? m_Impl->baseKey : m_Impl->sfKey;
	key->status->setText(status == KeyStatus::valid ? "Key accepted" : status == KeyStatus::invalid ? "Check this key" : status == KeyStatus::not_installed ? "Not selected for installation" : "Enter all 20 characters");
}
void Window::SetProgress(const Progress& progress) {
	auto& i = *m_Impl;
	if (Shown() != Page::install || i.cancelling) return;
	auto total = progress.totalBytes ? progress.totalBytes : progress.totalFiles;
	auto done = progress.totalBytes ? progress.completedBytes : progress.completedFiles;
	auto fraction = total ? std::clamp(double(done) / double(total), 0.0, 1.0) : 0.0;
	int row = -1;
	if (progress.phase == "disc" || progress.phase == "disc-xp1" || progress.phase == "preserveRenderer") row = 0;
	else if (progress.phase == "patch141" || progress.phase == "patch141-xp1") row = 1;
	else if (progress.phase == "overlay150") row = 2;
	else if (progress.phase == "verify") row = 3;
	if (row < 0) {
		i.installStatus->setText(total ? QString("Checking your media… %1%").arg(qRound(fraction * 100)) : "Checking your media…");
		return;
	}
	i.installStatus->setText(row == 3 ? "Verifying installed game data…" : progress.phase == "patch141-xp1" ? "Applying the Special Forces patch…" : "Installing game data…");
	// Completed stages remain full, including the DVD while its renderer is preserved.
	for (int previous = 0; previous < row + (progress.phase == "preserveRenderer" ? 1 : 0); ++previous) {
		auto bar = i.installProgress[previous];
		bar->setRange(0, 1000); bar->setValue(1000); bar->setFormat("Done");
	}
	if (progress.phase == "preserveRenderer") return;
	auto bar = i.installProgress[row];
	bar->setRange(0, total ? 1000 : 0); bar->setFormat("%p%");
	if (total) bar->setValue(qRound(fraction * 1000));
}
void Window::SetStatus(const std::string& value) {
	if (Shown() != Page::install) return;
	m_Impl->cancelling = true;
	m_Impl->next->setEnabled(false);
	m_Impl->installStatus->setText(text(value));
}
LaunchOptions Window::Options() const {
	auto& i = *m_Impl; LaunchOptions options;
	options.mod = i.mods->currentData().toString().toStdString(); options.skip_movies = i.movies->isChecked(); options.fullscreen = i.fullscreen->isChecked();
	options.width = i.width->value(); options.height = i.height->value(); options.port = i.port->value();
	options.server = i.server->text().toStdString(); options.password = i.password->text().toStdString(); options.player = i.player->text().toStdString();
	options.always_show = i.always->isChecked(); if (i.unlock->isChecked()) options.override = i.command->text().toStdString(); return options;
}
void Window::SetCommandLine(const std::string& line) { if (!m_Impl->unlock->isChecked()) m_Impl->command->setText(text(line)); }
assistant::VoiceSettings Window::Voice() const { return {m_Impl->playback, m_Impl->volume->value() / 1000.0, m_Impl->threshold->value() / 1000.0, m_Impl->boost->isChecked()}; }
void Window::SetMicrophone(MicrophoneStatus status) {
	auto& i = *m_Impl; i.microphone->setVisible(status != MicrophoneStatus::ready && status != MicrophoneStatus::skipped);
	i.microphone->setText(status == MicrophoneStatus::ask ? "Allow microphone" : status == MicrophoneStatus::denied ? "Open audio settings" : "Retry microphone");
	switch (status) {
	case MicrophoneStatus::skipped: i.micStatus->setText("Microphone check skipped in test mode."); break;
	case MicrophoneStatus::ask: i.micStatus->setText("Allow microphone access to test your level."); break;
	case MicrophoneStatus::denied: i.micStatus->setText("Microphone access was denied. You can skip voice setup."); break;
	case MicrophoneStatus::ready: i.micStatus->setText("Microphone ready. Speak to test your level."); break;
	case MicrophoneStatus::missing: i.micStatus->setText("Could not open a microphone. Connect or enable an input, then retry, or skip voice setup."); break;
	case MicrophoneStatus::capture_error: i.micStatus->setText("Microphone capture stopped. Retry or skip voice setup."); i.meter->setValue(0); break;
	}
}
void Window::SetLevel(double peak) { if (Shown() == Page::voice) m_Impl->meter->setValue(int(std::clamp(peak * Voice().captureVolume, 0.0, 1.0) * 1000)); }
void Window::Message(const std::string& title, const std::string& detail) {
	if (m_Impl->hidden) return;
	m_Impl->shell.show(); auto box = new QMessageBox(QMessageBox::Information, text(title), text(detail), QMessageBox::Ok, &m_Impl->shell);
	box->setTextFormat(Qt::PlainText); box->setAttribute(Qt::WA_DeleteOnClose); box->open();
}
void Window::Reveal(const std::filesystem::path& directory) { QDesktopServices::openUrl(QUrl::fromLocalFile(text(directory.string()))); }
void Window::StayHidden() { m_Impl->hidden = true; m_Impl->shell.hide(); }
Page Window::Shown() const { return m_Impl->shown; }
void Window::Click(Kind button) {
	auto& i = *m_Impl;
	auto target = button == Kind::next ? i.next : button == Kind::back ? i.back : button == Kind::voice_setup ? i.voice : button == Kind::verify ? i.verify : button == Kind::voice_skip ? i.skip : button == Kind::microphone ? i.microphone : nullptr;
	if (target) target->click(); else if (button == Kind::close) i.shell.close();
}
void Window::Choose(Kind kind, const std::filesystem::path& file) { (kind == Kind::disc_chosen ? m_Impl->disc : kind == Kind::patch141_chosen ? m_Impl->patch141 : m_Impl->patch150)->select(text(file.string())); }
void Window::PasteKey(KeyEdition row, const std::string& value) { (row == KeyEdition::BF2 ? m_Impl->baseKey : m_Impl->sfKey)->set(text(value)); }
void Window::Snapshot(const std::filesystem::path& png) { m_Impl->shell.ensurePolished(); m_Impl->shell.layout()->activate(); if (!m_Impl->shell.grab().save(text(png.string()))) throw std::runtime_error("Could not save window snapshot"); }
