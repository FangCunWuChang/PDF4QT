//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "pdfwidgetutils.h"

SettingsDialog::SettingsDialog(QWidget* parent, const pdf::PDFCMSSettings& settings, const pdf::PDFCMSManager* manager) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    m_settings(settings)
{
    ui->setupUi(this);

    ui->cmsProofingIntentComboBox->addItem(tr("Auto"), int(pdf::RenderingIntent::Auto));
    ui->cmsProofingIntentComboBox->addItem(tr("Perceptual"), int(pdf::RenderingIntent::Perceptual));
    ui->cmsProofingIntentComboBox->addItem(tr("Relative colorimetric"), int(pdf::RenderingIntent::RelativeColorimetric));
    ui->cmsProofingIntentComboBox->addItem(tr("Absolute colorimetric"), int(pdf::RenderingIntent::AbsoluteColorimetric));
    ui->cmsProofingIntentComboBox->addItem(tr("Saturation"), int(pdf::RenderingIntent::Saturation));

    for (const pdf::PDFColorProfileIdentifier& identifier : manager->getCMYKProfiles())
    {
        ui->cmsProofingColorProfileComboBox->addItem(identifier.name, identifier.id);
    }

    ui->cmsProofingIntentComboBox->setCurrentIndex(ui->cmsProofingIntentComboBox->findData(int(m_settings.proofingIntent)));
    ui->cmsProofingColorProfileComboBox->setCurrentIndex(ui->cmsProofingColorProfileComboBox->findData(m_settings.softProofingProfile));

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(320, 160)));
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::accept()
{
    m_settings.proofingIntent = static_cast<pdf::RenderingIntent>(ui->cmsProofingIntentComboBox->currentData().toInt());
    m_settings.softProofingProfile = ui->cmsProofingColorProfileComboBox->currentData().toString();

    QDialog::accept();
}