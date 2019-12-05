/*

ESP8266 file system builder

Copyright (C) 2016-2019 by Xose Pérez <xose dot perez at gmail dot com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*eslint quotes: ['error', 'single']*/
/*eslint-env es6*/

// -----------------------------------------------------------------------------
// Dependencies
// -----------------------------------------------------------------------------

const gulp = require('gulp');
const through = require('through2');
const PluginError = require('plugin-error');

const htmlmin = require('gulp-htmlmin');
const inline = require('gulp-inline');
const inlineImages = require('gulp-css-base64');
const favicon = require('gulp-base64-favicon');
const crass = require('gulp-crass');

const htmllint = require('gulp-htmllint');
const csslint = require('gulp-csslint');

const rename = require('gulp-rename');
const replace = require('gulp-replace');
const remover = require('gulp-remove-code');
const gzip = require('gulp-gzip');
const path = require('path');

const babel = require('@babel/core')
const terser = require('terser')

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

const htmlFolder = 'html/';
const configFolder = 'espurna/config/';
const dataFolder = 'espurna/data/';
const staticFolder = 'espurna/static/';

// -----------------------------------------------------------------------------
// Methods
// -----------------------------------------------------------------------------

var toHeader = function(name, debug) {

    return through.obj(function (source, encoding, callback) {

        var parts = source.path.split(path.sep);
        var filename = parts[parts.length - 1];
        var safename = name || filename.split('.').join('_');

        // Generate output
        var output = '';
        output += '#define ' + safename + '_len ' + source.contents.length + '\n';
        output += 'const uint8_t ' + safename + '[] PROGMEM = {';
        for (var i=0; i<source.contents.length; i++) {
            if (i > 0) { output += ','; }
            if (0 === (i % 20)) { output += '\n'; }
            output += '0x' + ('00' + source.contents[i].toString(16)).slice(-2);
        }
        output += '\n};';

        // clone the contents
        var destination = source.clone();
        destination.path = source.path + '.h';
        destination.contents = Buffer.from(output);

        if (debug) {
            console.info('Image ' + filename + ' \tsize: ' + source.contents.length + ' bytes');
        }

        callback(null, destination);

    });

};

var htmllintReporter = function(filepath, issues) {
    if (issues.length > 0) {
        issues.forEach(function (issue) {
            console.info(
                '[gulp-htmllint] ' +
                filepath + ' [' +
                issue.line + ',' +
                issue.column + ']: ' +
                '(' + issue.code + ') ' +
                issue.msg
            );
        });
        process.exitCode = 1;
    }
};

var transpileJs = function() {

    return through.obj(function (source, encoding, callback) {

        if (source.isNull()) {
            callback(null, source);
            return;
        }

        if (!source.path.endsWith("custom.js")) {
            callback(null, source);
            return;
        }

        // XXX: see https://github.com/babel/minify/issues/904
        //      `{builtIns: true}`` will crash when building *default* target
        //      nothing breaks when building each one individually
        const opts = {
            comments: false,
            presets: ['@babel/env']
        };

        let _this = this;
        const resultCb = function(result) {
            if (result) {
                source.contents = Buffer.from(result.code);
            }
            _this.push(source);
            callback();
        };

        const errorCb = function(error) {
            _this.emit('error', error);
            callback();
        };

        babel.transformAsync(source.contents, opts)
            .then(resultCb)
            .catch(errorCb);

    });

};

var compressJs = function() {

    return through.obj(function (source, encoding, callback) {

        if (source.isNull()) {
            callback(null, source);
            return;
        }

        if (!source.path.endsWith("custom.js")) {
            callback(null, source);
            return;
        }

        source.contents = Buffer.from(terser.minify(source.contents.toString()).code);
        this.push(source);
        callback();

    });

};

var buildWebUI = function(module) {

    var modules = {
        'light': false,
        'sensor': false,
        'rfbridge': false,
        'rfm69': false,
        'thermostat': false,
        'lightfox': false
    };
    if ('all' === module) {
        Object.keys(modules).forEach(function(key) {
            // Note: only build these when specified as module arg
            if (key === "rfm69") return;
            if (key === "lightfox") return;
            modules[key] = true;
        });
    } else if ('small' !== module) {
        modules[module] = true;
    }

    // Note: different comment styles in html and js
    // 1st call for the global html, 2nd for the custom.js
    return gulp.src(htmlFolder + '*.html').
        pipe(remover(modules)).
        pipe(htmllint({
            'failOnError': true,
            'rules': {
                'id-class-style': false,
                'label-req-for': false,
                'line-no-trailing-whitespace': false,
                'line-end-style': false,
            }
        }, htmllintReporter)).
        pipe(favicon()).
        pipe(inline({
            base: htmlFolder,
            js: [function() { return remover(modules); }, transpileJs, compressJs],
            css: [crass, inlineImages],
            disabledTypes: ['svg', 'img']
        })).
        pipe(htmlmin({
            collapseWhitespace: true,
            removeComments: true,
            minifyCSS: true,
            minifyJS: false
        })).
        pipe(replace('pure-', 'p-')).
        pipe(gzip()).
        pipe(rename('index.' + module + '.html.gz')).
        pipe(gulp.dest(dataFolder)).
        pipe(toHeader('webui_image', true)).
        pipe(gulp.dest(staticFolder));

};

// -----------------------------------------------------------------------------
// Tasks
// -----------------------------------------------------------------------------

gulp.task('certs', function() {
    gulp.src(dataFolder + 'server.*').
        pipe(toHeader(debug=false)).
        pipe(gulp.dest(staticFolder));
});

gulp.task('csslint', function() {
    gulp.src(htmlFolder + '*.css').
        pipe(csslint({ids: false})).
        pipe(csslint.formatter());
});

gulp.task('webui_small', function() {
    return buildWebUI('small');
});

gulp.task('webui_sensor', function() {
    return buildWebUI('sensor');
});

gulp.task('webui_light', function() {
    return buildWebUI('light');
});

gulp.task('webui_rfbridge', function() {
    return buildWebUI('rfbridge');
});

gulp.task('webui_rfm69', function() {
    return buildWebUI('rfm69');
});

gulp.task('webui_lightfox', function() {
    return buildWebUI('lightfox');
});

gulp.task('webui_thermostat', function() {
    return buildWebUI('thermostat');
});

gulp.task('webui_all', function() {
    return buildWebUI('all');
});

gulp.task('webui',
    gulp.parallel(
        'webui_small',
        'webui_sensor',
        'webui_light',
        'webui_rfbridge',
        'webui_rfm69',
        'webui_lightfox',
        'webui_thermostat',
        'webui_all'
    )
);

gulp.task('default', gulp.series('webui'));
