#pragma once
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

class ProjectSetupDialog : public QDialog
{
    Q_OBJECT

  public:
    ProjectSetupDialog(QWidget* parent = nullptr) : QDialog(parent)
    {
        // 创建界面
        setupUI();

        QString cfgFile = QCoreApplication::applicationDirPath() + "/config.ini";
        QSettings settings(cfgFile, QSettings::IniFormat);
        QString defaultPath = settings.value("ProjectSetupDialog/defaultPath").toString();
        if (!defaultPath.isEmpty())
        {
            m_pathEdit->setText(defaultPath);
            m_defaultWorkDirCheck->setChecked(true);
        }

        // 连接信号槽
        connect(m_browseButton, &QPushButton::clicked, this,
                &ProjectSetupDialog::browseProjectPath);
        connect(m_okButton, &QPushButton::clicked, this, &ProjectSetupDialog::onOkClicked);
        connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString projectName() const
    {
        return m_nameEdit->text().trimmed();
    }
    QString projectPath() const
    {
        return m_pathEdit->text().trimmed();
    }

  private slots:
    void browseProjectPath()
    {
        QString dir = QFileDialog::getExistingDirectory(this, "选择项目路径", QDir::homePath(),
                                                        QFileDialog::ShowDirsOnly |
                                                            QFileDialog::DontResolveSymlinks);

        if (!dir.isEmpty())
        {
            m_pathEdit->setText(QDir::toNativeSeparators(dir));
        }
    }

    void onOkClicked()
    {
        QString name = projectName();
        QString path = projectPath();

        // 验证输入
        if (name.isEmpty())
        {
            QMessageBox::warning(this, "输入错误", "项目名称不能为空！");
            return;
        }

        if (path.isEmpty())
        {
            QMessageBox::warning(this, "输入错误", "请选择项目路径！");
            return;
        }

        // 检查路径是否存在
        QDir dir(path);
        if (!dir.exists())
        {
            if (QMessageBox::question(this, "路径不存在", "指定路径不存在，是否创建？",
                                      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            {
                if (!dir.mkpath(path))
                {
                    QMessageBox::critical(this, "错误", "无法创建目录！");
                    return;
                }
            }
            else
            {
                return;
            }
        }

        // 检查路径是否可写
        QFile testFile(path + "/test_write.tmp");
        if (testFile.open(QIODevice::WriteOnly))
        {
            testFile.remove();
        }
        else
        {
            QMessageBox::critical(this, "错误", "路径不可写，请选择其他位置！");
            return;
        }

        QString cfgFile = QCoreApplication::applicationDirPath() + "/config.ini";
        QSettings settings(cfgFile, QSettings::IniFormat);
        if (m_defaultWorkDirCheck->isChecked())
        {
            settings.setValue("ProjectSetupDialog/defaultPath", projectPath());
        }
        else
        {
            settings.remove("ProjectSetupDialog/defaultPath");
        }

        // 所有验证通过，接受对话框
        accept();
    }

  private:
    void setupUI()
    {
        // 创建控件
        QLabel* nameLabel = new QLabel("项目名称:");
        m_nameEdit = new QLineEdit;
        m_nameEdit->setPlaceholderText("请输入项目名称");

        QLabel* pathLabel = new QLabel("项目路径:");
        m_pathEdit = new QLineEdit;
        m_pathEdit->setReadOnly(true);
        m_pathEdit->setPlaceholderText("请选择项目路径");

        m_browseButton = new QPushButton("浏览...");
        m_okButton = new QPushButton("确定");
        m_cancelButton = new QPushButton("取消");
        m_defaultWorkDirCheck = new QCheckBox("设为默认工作路径");

        // 创建布局
        QVBoxLayout* mainLayout = new QVBoxLayout;

        // 项目名称布局
        QHBoxLayout* nameLayout = new QHBoxLayout;
        nameLayout->addWidget(nameLabel);
        nameLayout->addWidget(m_nameEdit);

        // 项目路径布局
        QHBoxLayout* pathLayout = new QHBoxLayout;
        pathLayout->addWidget(pathLabel);
        pathLayout->addWidget(m_pathEdit);
        pathLayout->addWidget(m_browseButton);

        // 按钮布局
        QHBoxLayout* buttonLayout = new QHBoxLayout;
        buttonLayout->addStretch();
        buttonLayout->addWidget(m_okButton);
        buttonLayout->addWidget(m_cancelButton);

        // 添加所有布局到主布局
        mainLayout->addLayout(nameLayout);
        mainLayout->addLayout(pathLayout);
        mainLayout->addWidget(m_defaultWorkDirCheck);
        mainLayout->addLayout(buttonLayout);
        setLayout(mainLayout);
        setWindowTitle("新建项目");
        resize(500, 150);
    }

    QLineEdit* m_nameEdit;
    QLineEdit* m_pathEdit;
    QPushButton* m_browseButton;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    QCheckBox* m_defaultWorkDirCheck;
};
