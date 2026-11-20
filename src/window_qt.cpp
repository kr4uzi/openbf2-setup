#include "window.h"
#include "platform_qt.h"

#include <QApplication>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
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
#include <QProxyStyle>
#include <QPushButton>
#include <QLocale>
#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <map>
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

#if defined(Q_OS_MACOS)
// Qt's macOS style places the text of a push button and of a drop-down list for a face at the top of the control,
// but draws the face in the control's middle: the text sits about 2 points too high. It is moved to the middle
class MacButtonStyle : public QProxyStyle {
public:
	using QProxyStyle::QProxyStyle;
	// the style's layout margins of buttons and drop-down lists are uneven, so layouts place their faces off the
	// middle of a row; laid out by their plain rectangles, whose middle their face is, they line up with the labels
	void polish(QWidget* widget) override {
		QProxyStyle::polish(widget);
		if (qobject_cast<QPushButton*>(widget) || qobject_cast<QComboBox*>(widget))
			widget->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	}
	using QProxyStyle::polish;
	void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override {
		auto offset = 0.0;
		if (element == CE_PushButtonLabel && widget)
			offset = QRectF(widget->rect()).center().y() - QRectF(option->rect).center().y();
		else if (element == CE_ComboBoxLabel && widget)
			if (auto box = qstyleoption_cast<const QStyleOptionComboBox*>(option))
				offset = QRectF(widget->rect()).center().y() - QRectF(subControlRect(CC_ComboBox, box, SC_ComboBoxEditField, widget)).center().y();
		if (offset == 0.0) return QProxyStyle::drawControl(element, option, painter, widget);
		painter->save();
		painter->translate(0, offset);
		QProxyStyle::drawControl(element, option, painter, widget);
		painter->restore();
	}
};
#endif

// the online services of the launcher page: the game's own, OpenSpy, BF2Hub or another provider, what the provider
// file holds, and Configure… for the two which take one. a provider file dropped on the row is chosen as well
class OnlineRow : public QWidget {
public:
	std::function<void(const std::filesystem::path&)> dropped;
	QComboBox* choice;
	QLabel* status;
	QPushButton* configure;
	OnlineRow() {
		setAcceptDrops(true); setObjectName("onlineServices");
		auto layout = new QHBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
		choice = new QComboBox; choice->setObjectName("onlineServicesChoice"); choice->setAccessibleName("Online services");
		choice->addItem("Retail (default)", QString()); choice->addItem("OpenSpy", QString("openspy"));
		choice->addItem("BF2Hub", QString("bf2hub")); choice->addItem("Other", QString("other"));
		// the status goes on a line of its own below the row (see the launcher page)
		status = new QLabel; status->setObjectName("onlineServicesStatus"); status->setTextFormat(Qt::PlainText); status->setForegroundRole(QPalette::PlaceholderText);
		configure = new QPushButton("Configure…"); configure->setObjectName("onlineServicesConfigure");
		layout->addWidget(choice, 1); layout->addWidget(configure);
	}
protected:
	void dragEnterEvent(QDragEnterEvent* event) override {
		if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile()) event->acceptProposedAction();
	}
	void dropEvent(QDropEvent* event) override {
		if (dropped) dropped(event->mimeData()->urls().first().toLocalFile().toStdWString());
		event->acceptProposedAction();
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
	QWidget *windowSize, *windowPosition;
	QDialog* advanced;
	QToolButton* unlock;
	QPushButton* advancedButton;
	QComboBox* clientChoice;
	QCheckBox* tutorial;
	QLineEdit* punkbuster;
	// the demo to play: a recording found in the game's Demos folder, one chosen anywhere, or a path typed
	QComboBox* demo;
	// Setup/Demo.con, which plays a recording; carried with the options
	std::string demoConfig;
	QCheckBox *noSound, *multi, *skipDxCheck;
	// the options when the dialog opened: Cancel goes back to them
	LaunchOptions beforeAdvanced;
	QSpinBox *positionX, *positionY;
	QPushButton *next, *back, *microphone, *skip, *voice, *verify, *modsFolder;
	QLabel *heading, *installStatus, *micStatus, *profileNote, *launcherNote, *optionHint;
	// the online services: the row of the launcher page, the kept provider files of "bf2hub" and "other", where they
	// came from, and the choice to go back to when the dialog is cancelled
	OnlineRow* onlineRow;
	std::map<std::string, ProviderServices> kept;
	std::string bf2hubSource, otherSource, otherUrl;
	int onlineBefore = 0;
	// the provider dialog, for BF2Hub or for another provider
	QDialog* providerDialog;
	std::string providerMode;
	QLabel *providerTitle, *providerIntro;
	QRadioButton *automaticChoice, *hostedChoice, *urlChoice, *fileChoice;
	QWidget *automaticRow, *urlRow;
	QLineEdit *installerPath, *urlField, *filePath;
	QPushButton* providerOk;
	QNetworkAccessManager* network = nullptr;
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
#if defined(Q_OS_MACOS)
		if (!dynamic_cast<MacButtonStyle*>(QApplication::style())) QApplication::setStyle(new MacButtonStyle(QApplication::style()->name()));
#endif
		shell.setWindowTitle("OpenBF2");
		shell.setMinimumSize(660, 520);
		shell.resize(700, 560);
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
		next = new QPushButton("Continue"); next->setDefault(true); next->setObjectName("nextButton");
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
		skip = new QPushButton("Skip voice setup");
		auto skipRow = new QHBoxLayout; skipRow->addWidget(skip); skipRow->addStretch(); audio->addLayout(skipRow);
		QObject::connect(skip, &QPushButton::clicked, &shell, [this] { send(Kind::voice_skip); }); audio->addStretch();
		auto launcher = page();
		launcher->setContentsMargins(0, 12, 0, 0); launcher->setSpacing(12);
		auto preferences = new QGridLayout; preferences->setHorizontalSpacing(20); preferences->setVerticalSpacing(10);
		preferences->setColumnStretch(0, 1); preferences->setColumnStretch(1, 1);
		always = new QCheckBox("Open launcher on startup"); always->setObjectName("alwaysShowLauncher");
		always->setToolTip("Show this page every time OpenBF2 opens. Otherwise the game starts directly");
		preferences->addWidget(always, 0, 0);
		optionHint = new QLabel(frontend::launcherHint()); optionHint->setObjectName("launcherHint");
		auto smallFont = optionHint->font(); smallFont.setPointSizeF(smallFont.pointSizeF() - 1); optionHint->setFont(smallFont);
		optionHint->setForegroundRole(QPalette::PlaceholderText); preferences->addWidget(optionHint, 0, 1);
		movies = new QCheckBox("Skip the intro movies"); movies->setObjectName("skipMovies"); movies->setToolTip("Start without the intro movies (+restart 1)");
		tutorial = new QCheckBox("Show tutorial messages"); tutorial->setObjectName("tutorialMessages");
		tutorial->setToolTip("The tutorial hints during play, as set in the game's options. Unchecked hides them for this start (+tutorialMessages 0)");
		preferences->addWidget(tutorial, 2, 0); preferences->addWidget(movies, 1, 0);
		fullscreen = new QCheckBox("Fullscreen"); fullscreen->setObjectName("fullscreen");
		fullscreen->setToolTip("Fullscreen is the game's default. Windowed mode passes +fullscreen 0"); preferences->addWidget(fullscreen, 1, 1);
		windowSize = new QWidget; windowSize->setObjectName("windowSize");
		auto retained = windowSize->sizePolicy(); retained.setRetainSizeWhenHidden(true); windowSize->setSizePolicy(retained);
		auto size = new QHBoxLayout(windowSize); size->setContentsMargins(0, 0, 0, 0);
		width = spin(0, 16384); height = spin(0, 16384); width->setSpecialValueText("Default"); height->setSpecialValueText("Default");
		width->setMinimumWidth(96); height->setMinimumWidth(96);
		width->setAccessibleName("Window width"); height->setAccessibleName("Window height");
		width->setToolTip("The window's width (+szx); Default keeps the game's video settings");
		height->setToolTip("The window's height (+szy); Default keeps the game's video settings");
		size->addWidget(new QLabel("Window size")); size->addWidget(width); size->addWidget(new QLabel("×")); size->addWidget(height); size->addStretch();
		preferences->addWidget(windowSize, 2, 1); launcher->addLayout(preferences);
		auto form = new QGridLayout; form->setHorizontalSpacing(12); form->setVerticalSpacing(10); form->setColumnStretch(1, 1);
		mods = new QComboBox; mods->setAccessibleName("Mod"); mods->setObjectName("launcherMod");
		modsFolder = new QPushButton("Open Mods Folder"); modsFolder->setToolTip("The folder for your own mods. Each mod in it is offered above");
		QObject::connect(modsFolder, &QPushButton::clicked, &shell, [this] { send(Kind::mods_folder); });
		form->addWidget(new QLabel("Mod"), 0, 0); form->addWidget(mods, 0, 1); form->addWidget(modsFolder, 0, 2, 1, 2);
		server = new QLineEdit; server->setAccessibleName("Server address"); server->setPlaceholderText("address");
		port = spin(1, 65535); port->setAccessibleName("Server port"); port->setMaximumWidth(90); port->setToolTip("The server's port (+port), 16567 for most servers");
		player = new QLineEdit; player->setAccessibleName("Player name"); player->setPlaceholderText("the game’s own");
		player->setToolTip("The name to play with (+playerName); empty uses the profile's name");
		password = new QLineEdit; password->setAccessibleName("Server password"); password->setEchoMode(QLineEdit::Password); password->setPlaceholderText("none");
		password->setToolTip("The server's password, if it has one (+password)");
		form->addWidget(new QLabel("Player name"), 1, 0); form->addWidget(player, 1, 1);
		// the online services: the game's own, OpenSpy, or a provider file whose entries become +gamespyRedirect switches
		onlineRow = new OnlineRow;
		onlineRow->choice->setToolTip("Who provides the server list, logins and stats. OpenSpy needs nothing more; BF2Hub and Other take a provider file, which Configure… sets up. The command line below shows the switches");
		onlineRow->choice->setItemData(0, "The game's own services (gamespy.com)", Qt::ToolTipRole);
		onlineRow->choice->setItemData(1, "OpenSpy's services (+gamespyRedirect openspy.net); not verified by OpenBF2", Qt::ToolTipRole);
		onlineRow->choice->setItemData(2, "BF2Hub's services, from its installer, from the file OpenBF2 hosts, or from a file; not verified by OpenBF2", Qt::ToolTipRole);
		onlineRow->choice->setItemData(3, "Another provider's services, from a provider file on the web or on this computer", Qt::ToolTipRole);
		onlineRow->configure->setToolTip("Where the provider file comes from, and getting it again");
		QObject::connect(onlineRow->choice, &QComboBox::activated, &shell, [this] { chooseOnline(); });
		QObject::connect(onlineRow->configure, &QPushButton::clicked, &shell, [this] { openProvider(currentOnline()); });
		onlineRow->dropped = [this](const std::filesystem::path& file) { send(currentOnline() == "bf2hub" ? Kind::bf2hub_file : Kind::other_file, file); };
		buildProviderDialog();
		advancedButton = new QPushButton("Advanced Options…"); advancedButton->setObjectName("advancedOptions");
		advancedButton->setToolTip("Every further switch of the game: sound, instances, window position, demos, joining a server");
		form->addWidget(new QLabel("Online services"), 2, 0); form->addWidget(onlineRow, 2, 1); form->addWidget(advancedButton, 2, 2, 1, 2);
		auto smallStatus = onlineRow->status->font(); smallStatus.setPointSizeF(smallStatus.pointSizeF() - 1); onlineRow->status->setFont(smallStatus);
		form->addWidget(onlineRow->status, 3, 1, 1, 3);
		// Windows ships both clients: the 64-bit one, and the 32-bit one which PunkBuster needs
		clientChoice = new QComboBox; clientChoice->setObjectName("clientChoice"); clientChoice->setAccessibleName("Client");
		clientChoice->addItem("64-bit", QString("x64")); clientChoice->addItem("32-bit, for PunkBuster servers", QString("x86"));
		clientChoice->setToolTip("The 64-bit client, or the 32-bit one for servers which require PunkBuster (PunkBuster exists only for 32 bits)");
		auto clientLabel = new QLabel("Client");
		form->addWidget(clientLabel, 4, 0); form->addWidget(clientChoice, 4, 1);
#if !defined(Q_OS_WIN)
		clientLabel->hide(); clientChoice->hide();
#endif
		launcher->addLayout(form);
		buildAdvanced();
		auto arguments = new QHBoxLayout;
		auto argumentLabel = new QLabel("Arguments to bf2_client"); argumentLabel->setFont(smallFont); argumentLabel->setForegroundRole(QPalette::PlaceholderText);
		arguments->addWidget(argumentLabel); arguments->addStretch();
		launcherNote = new QLabel; launcherNote->setTextFormat(Qt::PlainText); launcherNote->setWordWrap(true); launcherNote->setFont(smallFont);
		launcherNote->setAlignment(Qt::AlignRight | Qt::AlignVCenter); arguments->addWidget(launcherNote, 1); launcher->addLayout(arguments);
		auto commandRow = new QHBoxLayout; commandRow->setSpacing(6);
		unlock = new QToolButton; unlock->setObjectName("commandLineLock"); unlock->setCheckable(true); unlock->setAutoRaise(true);
		unlock->setIconSize(QSize(20, 20)); unlock->setStyleSheet("QToolButton { border: none; background: transparent; }"); commandRow->addWidget(unlock);
		command = new QLineEdit; command->setObjectName("commandLine"); command->setAccessibleName("Command line"); command->setReadOnly(true);
		auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont); fixedFont.setPointSizeF(smallFont.pointSizeF()); command->setFont(fixedFont);
		commandRow->addWidget(command, 1); launcher->addLayout(commandRow); launcher->addStretch();
		describe();
		QWidget::setTabOrder(always, movies); QWidget::setTabOrder(movies, fullscreen); QWidget::setTabOrder(fullscreen, width);
		QWidget::setTabOrder(width, height); QWidget::setTabOrder(height, mods); QWidget::setTabOrder(mods, modsFolder);
		QWidget::setTabOrder(modsFolder, player); QWidget::setTabOrder(player, unlock); QWidget::setTabOrder(unlock, command);
		QWidget::setTabOrder(command, voice); QWidget::setTabOrder(voice, verify); QWidget::setTabOrder(verify, next);
		for (auto control : {movies, fullscreen, always}) QObject::connect(control, &QCheckBox::toggled, &shell, [this] { edited(); });
		QObject::connect(unlock, &QToolButton::toggled, &shell, [this] { edited(); });
		for (auto control : {width, height, port}) QObject::connect(control, &QSpinBox::valueChanged, &shell, [this] { edited(); });
		for (auto control : {server, password, player, command}) QObject::connect(control, &QLineEdit::textEdited, &shell, [this] { edited(); });
		QObject::connect(mods, &QComboBox::currentIndexChanged, &shell, [this] { edited(); });
		for (auto control : {noSound, multi}) QObject::connect(control, &QCheckBox::toggled, &shell, [this] { edited(); });
		for (auto control : {positionX, positionY}) QObject::connect(control, &QSpinBox::valueChanged, &shell, [this] { edited(); });
		QObject::connect(clientChoice, &QComboBox::currentIndexChanged, &shell, [this] { edited(); });
		QObject::connect(tutorial, &QCheckBox::toggled, &shell, [this] { edited(); });
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
	bool shown_page_is_launcher() const { return shown == Page::launcher; }
	// Advanced Options: every further switch of the engine which a player can use, in a dialog of its own. The
	// controls change the options at once, so the command line behind the dialog follows them; Cancel puts back
	// what was there when the dialog opened
	void buildAdvanced() {
		advanced = new QDialog(&shell); advanced->setObjectName("advancedOptionsDialog"); advanced->setWindowTitle("Advanced Options");
		advanced->setWindowModality(Qt::WindowModal);
		auto layout = new QVBoxLayout(advanced); layout->setContentsMargins(24, 20, 24, 16); layout->setSpacing(12);
		// a macOS sheet has no title bar
		auto title = new QLabel("Advanced Options"); auto titleFont = title->font(); titleFont.setBold(true); titleFont.setPointSizeF(titleFont.pointSizeF() + 2);
		title->setFont(titleFont); layout->addWidget(title);
		auto intro = new QLabel("The game gets these as command-line switches. The command line on the launcher page shows the result.");
		intro->setWordWrap(true); intro->setForegroundRole(QPalette::PlaceholderText); layout->addWidget(intro);
		auto grid = new QGridLayout; grid->setHorizontalSpacing(12); grid->setVerticalSpacing(8); grid->setColumnStretch(1, 1); grid->setColumnStretch(3, 1);
		noSound = new QCheckBox("Start without sound"); multi = new QCheckBox("Allow several instances");
		grid->addWidget(noSound, 1, 0, 1, 2); grid->addWidget(multi, 1, 2, 1, 2);
		windowPosition = new QWidget; auto position = new QHBoxLayout(windowPosition); position->setContentsMargins(0, 0, 0, 0);
		positionX = spin(-1, 32767); positionY = spin(-1, 32767); positionX->setSpecialValueText("System"); positionY->setSpecialValueText("System");
		positionX->setMinimumWidth(96); positionY->setMinimumWidth(96);
		positionX->setAccessibleName("Window left"); positionY->setAccessibleName("Window top");
		positionX->setToolTip("Windowed mode only"); positionY->setToolTip("Windowed mode only");
		position->addWidget(positionX); position->addWidget(new QLabel("×")); position->addWidget(positionY); position->addStretch();
		grid->addWidget(new QLabel("Window position"), 4, 0); grid->addWidget(windowPosition, 4, 1, 1, 3);
		demo = new QComboBox; demo->setEditable(true); demo->setInsertPolicy(QComboBox::NoInsert); demo->setAccessibleName("Demo file");
		demo->lineEdit()->setPlaceholderText("none; replaces joining a server");
		auto chooseDemo = new QPushButton("Choose…"), demosFolder = new QPushButton("Open Folder");
		chooseDemo->setToolTip("Choose a recorded demo (.bf2demo) or its demo options (.con) anywhere");
		demosFolder->setToolTip("The game's Demos folder, where it records and where demos are found");
		QObject::connect(chooseDemo, &QPushButton::clicked, &shell, [this] {
			auto file = QFileDialog::getOpenFileName(advanced, "Choose a demo", {}, "Demos (*.bf2demo *.con);;All files (*)");
			if (!file.isEmpty()) { setDemo(QDir::toNativeSeparators(file)); edited(); }
		});
		QObject::connect(demosFolder, &QPushButton::clicked, &shell, [this] { send(Kind::demos_folder); });
		auto demoRow = new QHBoxLayout; demoRow->setContentsMargins(0, 0, 0, 0);
		demoRow->addWidget(demo, 1); demoRow->addWidget(chooseDemo); demoRow->addWidget(demosFolder);
		grid->addWidget(new QLabel("Play a demo"), 5, 0); grid->addLayout(demoRow, 5, 1, 1, 3);
		// joining a server at start: the menu's server list is the usual way, so it is here and not on the page
		auto join = new QHBoxLayout; join->setContentsMargins(0, 0, 0, 0);
		port->setMaximumWidth(100); password->setMaximumWidth(160);
		join->addWidget(server, 1); join->addWidget(new QLabel("Port")); join->addWidget(port); join->addWidget(new QLabel("Password")); join->addWidget(password);
		grid->addWidget(new QLabel("Join a server"), 8, 0); grid->addLayout(join, 8, 1, 1, 3);
		// the DirectX check and PunkBuster exist only for the Windows clients
		skipDxCheck = new QCheckBox("Skip the DirectX check"); punkbuster = new QLineEdit;
		punkbuster->setAccessibleName("PunkBuster folder"); punkbuster->setPlaceholderText("the game’s pb folder; one per instance, 32-bit client");
		auto punkbusterLabel = new QLabel("PunkBuster folder");
		grid->addWidget(skipDxCheck, 6, 0, 1, 2); grid->addWidget(punkbusterLabel, 7, 0); grid->addWidget(punkbuster, 7, 1, 1, 3);
#if !defined(Q_OS_WIN)
		skipDxCheck->hide(); punkbusterLabel->hide(); punkbuster->hide();
#endif
		layout->addLayout(grid);
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
		layout->addWidget(buttons);
		QObject::connect(buttons, &QDialogButtonBox::accepted, advanced, &QDialog::accept);
		QObject::connect(buttons, &QDialogButtonBox::rejected, advanced, &QDialog::reject);
		QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, advanced, [this] {
			auto defaults = LaunchOptions{}; defaults.fullscreen = fullscreen->isChecked(); applyAdvanced(defaults); edited();
		});
		QObject::connect(advanced, &QDialog::rejected, &shell, [this] { applyAdvanced(beforeAdvanced); edited(); });

		QObject::connect(advancedButton, &QPushButton::clicked, &shell, [this] { openAdvanced(); });
		QObject::connect(skipDxCheck, &QCheckBox::toggled, &shell, [this] { edited(); });
		QObject::connect(punkbuster, &QLineEdit::textEdited, &shell, [this] { edited(); });
		QObject::connect(demo, &QComboBox::currentTextChanged, &shell, [this] { edited(); });
	}
	LaunchOptions currentOptions() const;
	std::string currentOnline() const { return onlineRow->choice->currentData().toString().toStdString(); }
	void selectOnline(const std::string& online) {
		auto index = onlineRow->choice->findData(text(online));
		onlineRow->choice->setCurrentIndex(index < 0 ? 0 : index);
		onlineBefore = onlineRow->choice->currentIndex();
	}
	// OpenSpy and the game's own take nothing more; BF2Hub and Other take their kept file, or the dialog asks for one.
	// the choice stays as it was until a file is accepted
	void chooseOnline() {
		auto online = currentOnline();
		if (provider_online(online) && !kept.contains(online)) {
			onlineRow->choice->setCurrentIndex(onlineBefore);
			openProvider(online);
			return;
		}
		onlineBefore = onlineRow->choice->currentIndex();
		fillProvider(); edited();
	}
	// the status of the row: where the provider file came from, and until when it holds
	void fillProvider() {
		auto online = currentOnline();
		onlineRow->configure->setVisible(provider_online(online));
		auto status = QString{};
		if (provider_online(online)) {
			auto found = kept.find(online);
			if (found == kept.end() || found->second.services.empty())
				status = "Provider file missing";
			else {
				auto source = online == "bf2hub" ? bf2hubSource : otherSource;
				status = source == "automatic" ? "From BF2Hub's installer" : source == "hosted" ? "Hosted by OpenBF2"
					: source == "url" ? "From " + QUrl(text(otherUrl)).host() : "From a file";
				if (online == "other") status = text(found->second.name) + " · " + status.left(1).toLower() + status.mid(1);
				if (auto expiry = provider_expiry(found->second)) {
					auto when = QDateTime::fromSecsSinceEpoch(expiry->time_since_epoch().count());
					status += when <= QDateTime::currentDateTime() ? QString(" · out of date") : " · valid until " + QLocale().toString(when.date(), QLocale::ShortFormat);
				}
			}
		}
		onlineRow->status->setText(status); onlineRow->status->setVisible(!status.isEmpty());
		if (shown == Page::launcher) fitLauncher();
	}
	void buildProviderDialog() {
		providerDialog = new QDialog(&shell); providerDialog->setObjectName("providerDialog"); providerDialog->setWindowModality(Qt::WindowModal);
		auto layout = new QVBoxLayout(providerDialog); layout->setContentsMargins(24, 20, 24, 16); layout->setSpacing(10);
		providerTitle = new QLabel; providerTitle->setObjectName("providerTitle"); auto titleFont = providerTitle->font(); titleFont.setBold(true); titleFont.setPointSizeF(titleFont.pointSizeF() + 2);
		providerTitle->setFont(titleFont); layout->addWidget(providerTitle);
		providerIntro = new QLabel; providerIntro->setWordWrap(true); providerIntro->setForegroundRole(QPalette::PlaceholderText); layout->addWidget(providerIntro);
		auto browseRow = [this](QLineEdit*& field, const QString& placeholder, const QString& filter, const QString& title) {
			auto row = new QWidget; auto box = new QHBoxLayout(row); box->setContentsMargins(24, 0, 0, 0);
			field = new QLineEdit; field->setPlaceholderText(placeholder);
			auto browse = new QPushButton("Choose…");
			box->addWidget(field, 1); box->addWidget(browse);
			QObject::connect(browse, &QPushButton::clicked, providerDialog, [this, &field, filter, title] {
				auto file = QFileDialog::getOpenFileName(providerDialog, title, {}, filter);
				if (!file.isEmpty()) { field->setText(QDir::toNativeSeparators(file)); updateProviderOk(); }
			});
			QObject::connect(field, &QLineEdit::textChanged, providerDialog, [this] { updateProviderOk(); });
			return row;
		};
		automaticChoice = new QRadioButton("Automatic: ask the BF2Hub client in its installer");
		automaticChoice->setToolTip("Download the BF2Hub client installer from bf2hub.com first. Nothing of it is run or kept: its client is asked for BF2Hub's addresses");
		automaticRow = browseRow(installerPath, "the BF2Hub installer you downloaded from bf2hub.com", "BF2Hub installer (*.exe);;All files (*)", "Choose the BF2Hub installer");
		installerPath->setAccessibleName("BF2Hub installer");
		hostedChoice = new QRadioButton(QString("Hosted by OpenBF2 (%1)").arg(QUrl(bf2hub_hosted_url).host()));
		hostedChoice->setToolTip(QString("BF2Hub's provider file as OpenBF2 publishes it: %1. Downloaded again when it runs out").arg(bf2hub_hosted_url));
		urlChoice = new QRadioButton("From the web");
		urlChoice->setToolTip("A provider file a provider publishes, downloaded from this address; downloaded again when it runs out");
		urlRow = new QWidget; { auto box = new QHBoxLayout(urlRow); box->setContentsMargins(24, 0, 0, 0);
			urlField = new QLineEdit; urlField->setPlaceholderText("https://…"); urlField->setAccessibleName("Provider file address"); box->addWidget(urlField);
			QObject::connect(urlField, &QLineEdit::textChanged, providerDialog, [this] { updateProviderOk(); }); }
		fileChoice = new QRadioButton("A provider file on this computer");
		auto fileRow = browseRow(filePath, "a provider file (.json)", "Provider file (*.json);;All files (*)", "Choose a provider file");
		filePath->setAccessibleName("Provider file");
		for (auto widget : std::initializer_list<QWidget*>{automaticChoice, automaticRow, hostedChoice, urlChoice, urlRow, fileChoice, fileRow}) layout->addWidget(widget);
		for (auto radio : {automaticChoice, hostedChoice, urlChoice, fileChoice}) QObject::connect(radio, &QRadioButton::toggled, providerDialog, [this] { updateProviderOk(); });
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
		providerOk = buttons->button(QDialogButtonBox::Ok); providerOk->setText("Get");
		QObject::connect(buttons, &QDialogButtonBox::rejected, providerDialog, &QDialog::reject);
		QObject::connect(buttons, &QDialogButtonBox::accepted, providerDialog, [this] {
			providerDialog->accept();
			if (automaticChoice->isChecked()) send(Kind::bf2hub_installer, installerPath->text().trimmed().toStdWString());
			else if (hostedChoice->isChecked()) send(Kind::bf2hub_hosted);
			else if (urlChoice->isChecked()) send(Kind::other_url, urlField->text().trimmed().toStdString());
			else send(providerMode == "bf2hub" ? Kind::bf2hub_file : Kind::other_file, filePath->text().trimmed().toStdWString());
		});
	}
	void updateProviderOk() {
		providerOk->setEnabled((automaticChoice->isChecked() && !installerPath->text().trimmed().isEmpty()) || hostedChoice->isChecked()
			|| (urlChoice->isChecked() && valid_provider_url(urlField->text().trimmed().toStdString())) || (fileChoice->isChecked() && !filePath->text().trimmed().isEmpty()));
	}
	// BF2Hub: automatic (Windows), hosted or a file; another provider: an address or a file
	void openProvider(const std::string& online) {
		providerMode = online;
		auto bf2hub = online == "bf2hub";
		providerTitle->setText(bf2hub ? "BF2Hub" : "Other Online Services");
		providerIntro->setText(bf2hub ? "BF2Hub's services take a provider file with their addresses. OpenBF2 does not verify the service."
			: "Another provider's services take its provider file. OpenBF2 does not verify the service.");
#if defined(Q_OS_WIN)
		const bool automatic = bf2hub;
#else
		const bool automatic = false;
#endif
		automaticChoice->setVisible(automatic); automaticRow->setVisible(automatic); hostedChoice->setVisible(bf2hub);
		urlChoice->setVisible(!bf2hub); urlRow->setVisible(!bf2hub);
		auto source = bf2hub ? bf2hubSource : otherSource;
		urlField->setText(text(otherUrl));
		(source == "file" ? fileChoice : bf2hub ? (source == "automatic" && automatic ? automaticChoice : hostedChoice) : urlChoice)->setChecked(true);
		updateProviderOk(); providerDialog->adjustSize();
		if (!hidden) providerDialog->open();
	}
	// a tooltip on every setting, with the switch it gives the game
	void describe() {
		mods->setToolTip("The mod to start (+modPath mods/<mod>)");
		server->setToolTip("Join this server at start (+joinServer); empty starts into the menu");
		command->setToolTip("What the game is started with. Unlock to write it yourself");
		voice->setToolTip("Microphone level and voice chat defaults for new profiles");
		verify->setToolTip("Check every installed file against the official contents");
		noSound->setToolTip("Start the game without sound (+noSound 1)");
		multi->setToolTip("Allow several copies of the game at once (+multi 1)");
		positionX->setToolTip("The window's left edge (+wx), windowed mode only; System lets the system place it");
		positionY->setToolTip("The window's top edge (+wy), windowed mode only; System lets the system place it");
		demo->setToolTip("Play a recorded demo (+demo): a recording (.bf2demo) of the game's Demos folder, the newest first, or one chosen or typed; also a .con with its demo options. It replaces joining a server");
		skipDxCheck->setToolTip("Start without checking the DirectX version (+skipDXCheck 1). Use with caution");
		punkbuster->setToolTip("PunkBuster's folder, for several instances of the 32-bit client (+pbPath); empty uses the game's pb folder");
		specialForces->setToolTip("Install the Special Forces expansion from your disc as well; it needs its own CD key");
		volume->setToolTip("How loud your microphone is sent");
		threshold->setToolTip("How loud you must speak before voice chat sends");
		boost->setToolTip("Amplify a quiet microphone");
	}
	// a found demo by its path, otherwise the path as typed or chosen
	void setDemo(const QString& value) {
		auto index = value.isEmpty() ? -1 : demo->findData(value);
		if (index >= 0) demo->setCurrentIndex(index); else { demo->setCurrentIndex(-1); demo->setEditText(value); }
	}
	QString demoValue() const {
		auto index = demo->currentIndex();
		if (index >= 0 && demo->itemText(index) == demo->currentText()) return demo->itemData(index).toString();
		return demo->currentText().trimmed();
	}
	void openAdvanced() {
		beforeAdvanced = LaunchOptions{}; collectAdvanced(beforeAdvanced);
		advanced->adjustSize(); advanced->open();
	}
	void applyAdvanced(const LaunchOptions& options) {
		noSound->setChecked(options.no_sound); multi->setChecked(options.multi);
		positionX->setValue(options.window_x); positionY->setValue(options.window_y);
		setDemo(text(options.demo)); skipDxCheck->setChecked(options.skip_dx_check); punkbuster->setText(text(options.punkbuster));
		server->setText(text(options.server)); port->setValue(options.port); password->setText(text(options.password));
	}
	void collectAdvanced(LaunchOptions& options) const {
		options.no_sound = noSound->isChecked(); options.multi = multi->isChecked();
		options.window_x = positionX->value(); options.window_y = positionY->value();
		options.demo = demoValue().toStdString();
#if defined(Q_OS_WIN)
		options.skip_dx_check = skipDxCheck->isChecked(); options.punkbuster = punkbuster->text().trimmed().toStdString();
#endif
		options.server = server->text().toStdString(); options.port = port->value(); options.password = password->text().toStdString();
	}
	// at least the preferred size, and never smaller than the contents need: fonts and display scaling
	// differ between systems (Windows with 150 % scaling, a larger system font)
	void fit(QSize preferred) {
		shell.layout()->activate();
		auto needed = shell.sizeHint().expandedTo(shell.minimumSizeHint());
		shell.setMinimumSize(shell.minimumSize().expandedTo(shell.minimumSizeHint()));
		shell.resize(preferred.expandedTo(needed));
	}
	// the launcher page is as tall as its options need: the advanced section adds its height
	void fitLauncher() { fit(QSize(std::max(shell.width(), 700), 500)); }
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
		const bool locked = !unlock->isChecked();
		for (QWidget* control : std::initializer_list<QWidget*>{mods, movies, fullscreen, player, onlineRow, tutorial, clientChoice, advancedButton}) control->setEnabled(locked);

		// a demo replaces joining a server
		const bool playsDemo = !demoValue().isEmpty();
		for (QWidget* control : std::initializer_list<QWidget*>{server, port, password}) control->setEnabled(locked && !playsDemo);
		width->setEnabled(locked && !fullscreen->isChecked()); height->setEnabled(width->isEnabled());
		windowPosition->setEnabled(!fullscreen->isChecked());
		advancedButton->setText(has_advanced_options(currentOptions()) ? "Advanced Options (in use)…" : "Advanced Options…");
		send(Kind::options_edited);
	}
	void show(Page page, const QString& title, const QString& action, bool canBack) {
		bool wasLauncher = shown == Page::launcher, launcher = page == Page::launcher;
		shown = page; busy = false; pages->setEnabled(true); pages->setCurrentIndex(int(page));
		pages->updateGeometry();
		voice->setVisible(launcher); verify->setVisible(launcher); voice->setEnabled(true); verify->setEnabled(true);
		outer->setContentsMargins(launcher ? 40 : 28, launcher ? 38 : 24, launcher ? 40 : 28, 24);
		auto font = heading->font(); font.setPointSize(launcher ? 20 : 22); font.setBold(!launcher); heading->setFont(font);
		shell.setMinimumSize(launcher ? QSize(660, 480) : QSize(660, 520));
		if (launcher) fitLauncher(); else fit(launcher != wasLauncher ? QSize(700, 560) : shell.size());
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
task<std::string> Window::Download(std::string url) {
	auto& i = *m_Impl;
	if (!valid_provider_url(url)) throw std::runtime_error{ "The address must start with https:// and name a host." };
	if (!i.network) i.network = new QNetworkAccessManager(&i.shell);
	// the answer comes through Qt's event loop; the coroutine waits for it on its channel
	using Answer = boost::asio::experimental::channel<void(boost::system::error_code, std::string, std::string)>;
	auto answer = std::make_shared<Answer>(co_await boost::asio::this_coro::executor, 1);
	auto request = QNetworkRequest(QUrl(text(url)));
	// https all the way: a redirect may only go to https, and a slow host gives up
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(15000);
	request.setHeader(QNetworkRequest::UserAgentHeader, "OpenBF2-Setup");
	auto reply = i.network->get(request);
	auto body = std::make_shared<std::string>();
	QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, body] {
		body->append(reply->readAll().toStdString());
		if (body->size() > provider_file_limit) reply->abort();
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, body, answer] {
		body->append(reply->readAll().toStdString());
		auto failure = std::string{};
		if (body->size() > provider_file_limit) failure = "The file is larger than a provider file can be.";
		else if (reply->error() != QNetworkReply::NoError) failure = "The provider file could not be downloaded: " + reply->errorString().toStdString();
		else if (reply->url().scheme() != "https") failure = "The provider file was sent without https.";
		answer->try_send(boost::system::error_code{}, failure.empty() ? *body : std::string{}, failure);
		reply->deleteLater();
	});
	auto result = co_await answer->async_receive(boost::asio::as_tuple(boost::asio::use_awaitable));
	if (!std::get<2>(result).empty()) throw std::runtime_error{ std::get<2>(result) };
	co_return std::get<1>(result);
}
bool Window::LauncherRequested() const { return frontend::launcherRequested(); }
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
	i.demoConfig = options.demo_config;
	i.mods->clear(); for (const auto& mod : mods) i.mods->addItem(text(mod.title), text(mod.directory));
	i.mods->setCurrentIndex(i.mods->findData(text(options.mod))); i.movies->setChecked(options.skip_movies); i.fullscreen->setChecked(options.fullscreen);
	i.width->setValue(options.width); i.height->setValue(options.height); i.player->setText(text(options.player));
	i.always->setChecked(options.always_show); i.unlock->setChecked(!options.override.empty()); i.command->setText(text(options.override));
	i.clientChoice->setCurrentIndex(options.client == "x86" ? 1 : 0);
i.bf2hubSource = options.bf2hub_source; i.otherSource = options.other_source; i.otherUrl = options.other_url;
	if (provider_online(options.online) && !options.provider.services.empty()) i.kept[options.online] = options.provider;
	i.selectOnline(options.online); i.fillProvider();
	i.tutorial->setChecked(options.tutorial_messages);
	i.applyAdvanced(options);
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
LaunchOptions Window::Options() const { return m_Impl->currentOptions(); }
LaunchOptions Window::Impl::currentOptions() const {
	auto& i = *this; LaunchOptions options;
	options.mod = i.mods->currentData().toString().toStdString(); options.skip_movies = i.movies->isChecked(); options.fullscreen = i.fullscreen->isChecked();
	options.width = i.width->value(); options.height = i.height->value(); options.player = i.player->text().toStdString();
	options.always_show = i.always->isChecked(); if (i.unlock->isChecked()) options.override = i.command->text().toStdString();
#if defined(Q_OS_WIN)
	options.client = i.clientChoice->currentData().toString().toStdString();
#endif
	options.demo_config = i.demoConfig;
	options.online = i.currentOnline();
	options.bf2hub_source = i.bf2hubSource; options.other_source = i.otherSource; options.other_url = i.otherUrl;
	if (options.online == "openspy") options.provider = parse_provider(provider_presets().front().json);
	else if (auto found = i.kept.find(options.online); found != i.kept.end()) options.provider = found->second;
	options.tutorial_messages = i.tutorial->isChecked();
	i.collectAdvanced(options);
	return options;
}
void Window::SetProviders(const std::map<std::string, ProviderServices>& kept) {
	auto& i = *m_Impl;
	for (const auto& [online, services] : kept) i.kept[online] = services;
	i.fillProvider();
}
void Window::ShowProvider(const std::string& online, const std::string& source, const std::string& url, const ProviderServices& services) {
	auto& i = *m_Impl;
	i.kept[online] = services;
	(online == "bf2hub" ? i.bf2hubSource : i.otherSource) = source;
	if (online == "other") i.otherUrl = url;
	i.selectOnline(online); i.fillProvider(); i.edited();
}
void Window::SetDemos(const std::vector<std::filesystem::path>& demos) {
	auto& i = *m_Impl;
	auto value = i.demoValue();
	// a new list is no edit of the user's
	const QSignalBlocker quiet{i.demo};
	i.demo->clear();
	for (const auto& file : demos) {
		auto path = text(file.generic_string());
		i.demo->addItem(text(file.filename().string()), path); i.demo->setItemData(i.demo->count() - 1, path, Qt::ToolTipRole);
	}
	i.setDemo(value);
}
void Window::SnapshotProvider(const std::string& online, const std::filesystem::path& png) {
	auto& i = *m_Impl; i.openProvider(online);
	auto dialog = i.providerDialog; dialog->ensurePolished(); dialog->layout()->activate(); dialog->resize(dialog->sizeHint()); dialog->layout()->activate();
	if (!dialog->grab().save(text(png.string()))) throw std::runtime_error("Could not save dialog snapshot");
}
void Window::SnapshotAdvanced(const std::filesystem::path& png) {
	auto dialog = m_Impl->advanced; dialog->ensurePolished(); dialog->adjustSize(); dialog->layout()->activate();
	if (!dialog->grab().save(text(png.string()))) throw std::runtime_error("Could not save dialog snapshot");
}
void Window::SetCommandLine(const std::string& line) { if (!m_Impl->unlock->isChecked()) { m_Impl->command->setText(text(line)); m_Impl->command->setCursorPosition(0); } }
assistant::VoiceSettings Window::Voice() const { return {m_Impl->playback, m_Impl->volume->value() / 1000.0, m_Impl->threshold->value() / 1000.0, m_Impl->boost->isChecked()}; }
void Window::SetMicrophone(MicrophoneStatus status) {
	auto& i = *m_Impl; i.microphone->setVisible(status != MicrophoneStatus::ready && status != MicrophoneStatus::skipped);
	i.microphone->setText(status == MicrophoneStatus::ask ? "Allow microphone" : status == MicrophoneStatus::denied ? frontend::microphoneSettingsLabel() : "Retry microphone");
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
void Window::Message(const std::string& title, const std::string& detail, const std::string& more) {
	if (m_Impl->hidden) return;
	m_Impl->shell.show(); auto box = new QMessageBox(QMessageBox::Information, text(title), text(detail), QMessageBox::Ok, &m_Impl->shell);
	if (!more.empty()) box->setDetailedText(text(more));
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
void Window::Choose(Kind kind, const std::filesystem::path& file) {
	// a provider's file as the dialog or the drop would give it
	if (kind == Kind::bf2hub_installer || kind == Kind::bf2hub_hosted || kind == Kind::bf2hub_file || kind == Kind::other_url || kind == Kind::other_file) { m_Impl->send(kind, file); return; }
	(kind == Kind::disc_chosen ? m_Impl->disc : kind == Kind::patch141_chosen ? m_Impl->patch141 : m_Impl->patch150)->select(text(file.string())); }
void Window::PasteKey(KeyEdition row, const std::string& value) { (row == KeyEdition::BF2 ? m_Impl->baseKey : m_Impl->sfKey)->set(text(value)); }
void Window::Snapshot(const std::filesystem::path& png) { m_Impl->shell.ensurePolished(); m_Impl->shell.layout()->activate(); if (!m_Impl->shell.grab().save(text(png.string()))) throw std::runtime_error("Could not save window snapshot"); }
