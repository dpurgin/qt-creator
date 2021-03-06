/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company.  For licensing terms and
** conditions see http://www.qt.io/terms-conditions.  For further information
** use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file.  Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, The Qt Company gives you certain additional
** rights.  These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#ifndef BUILTINEDITORDOCUMENTPROCESSOR_H
#define BUILTINEDITORDOCUMENTPROCESSOR_H

#include "baseeditordocumentprocessor.h"
#include "builtineditordocumentparser.h"
#include "cppsemanticinfoupdater.h"
#include "cpptools_global.h"
#include "semantichighlighter.h"

namespace CppTools {

class CPPTOOLS_EXPORT BuiltinEditorDocumentProcessor : public BaseEditorDocumentProcessor
{
    Q_OBJECT
    BuiltinEditorDocumentProcessor();

public:
    BuiltinEditorDocumentProcessor(TextEditor::TextDocument *document,
                                   bool enableSemanticHighlighter = true);
    ~BuiltinEditorDocumentProcessor();

    // BaseEditorDocumentProcessor interface
    void run() Q_DECL_OVERRIDE;
    void semanticRehighlight(bool force) Q_DECL_OVERRIDE;
    CppTools::SemanticInfo recalculateSemanticInfo() Q_DECL_OVERRIDE;
    BaseEditorDocumentParser *parser() Q_DECL_OVERRIDE;
    CPlusPlus::Snapshot snapshot() Q_DECL_OVERRIDE;
    bool isParserRunning() const Q_DECL_OVERRIDE;

private:
    void onParserFinished(CPlusPlus::Document::Ptr document, CPlusPlus::Snapshot snapshot);
    void onSemanticInfoUpdated(const CppTools::SemanticInfo semanticInfo);
    void onCodeWarningsUpdated(CPlusPlus::Document::Ptr document,
                               const QList<CPlusPlus::Document::DiagnosticMessage> &codeWarnings);

    SemanticInfo::Source createSemanticInfoSource(bool force) const;

private:
    BuiltinEditorDocumentParser m_parser;
    QFuture<void> m_parserFuture;

    CPlusPlus::Snapshot m_documentSnapshot;
    QList<QTextEdit::ExtraSelection> m_codeWarnings;
    bool m_codeWarningsUpdated;

    SemanticInfoUpdater m_semanticInfoUpdater;
    QScopedPointer<SemanticHighlighter> m_semanticHighlighter;
};

} // namespace CppTools

#endif // BUILTINEDITORDOCUMENTPROCESSOR_H
