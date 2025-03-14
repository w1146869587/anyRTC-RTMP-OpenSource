package sign

import (
	"cfg"
	"database/sql"
	"os"

	"encoding/json"
	"fmt"
	"log"

	//"strings"

	"github.com/satori/go.uuid"

	//_ "github.com/denisenkom/go-mssqldb"
	_ "github.com/mattn/go-adodb"
)

const (
	StaSignupOk = iota
	StaSignupNameDuplicate
	StaSignupSqlServerError
)

const (
	StaSigninOk = iota
	StaSigninNameError
	StaSigninPwdError
	StaSigninSqlServerError
)

var (
	sessions = make(map[string]*UserInfo)
)

func SessionsDel(token string) {
	delete(sessions, token)
}
func SessionsSet(token string, ui *UserInfo) {
	sessions[token] = ui
}

func SessionsGet(token string) (*UserInfo, bool) {
	ui := &UserInfo{}
	ret := false
	found := false

	if ui, found = sessions[token]; found {
		log.Println("found", token, ui)
		ret = true
	} else {
		ui = &UserInfo{}
		tfile := "./tokens/" + token
		f, err3 := os.Open(tfile) //创建文件
		log.Println("openfile", tfile, err3)
		if err3 == nil {
			defer f.Close()
			fileinfo, err := f.Stat()
			if err == nil {
				fileSize := fileinfo.Size()
				buffer := make([]byte, fileSize)
				_, err := f.Read(buffer)
				if err == nil {
					//log.Println(string(buffer))
					ui0 := &UserInfo{}
					err3 := json.Unmarshal(buffer, &ui0)
					if err3 == nil {
						//log.Println(ui0)
						sessions[token] = ui0
						ret = true
						return ui0, ret
					}
				}
			}
		}
	}
	//log.Println("SessionsGet", token, ui, ret)
	return ui, ret
}

func SignIn(un string, pwd string) *UserInfo {
	ok, _ := uuid.NewV4()
	sid, _ := uuid.NewV4()
	ui := &UserInfo{UserName: "anonymous", UserId: 0, Token: ok.String(), UserUuid: "00000000-0000-0000-0000-000000000000", Info: "", AvatarUrl: "../../static/logo.png", SessionId: sid.String()}
	db, err0 := sql.Open("adodb", cfg.Cfg["mssql"])
	if err0 != nil {
		fmt.Println("sql open:", err0)
		ui.Info = "SignIn sql open error"
	} else {
		defer db.Close()
		stmt, err0 := db.Prepare(`SELECT [UserID],[UserName],[userguid] FROM [Dv_User] where [UserName] = ? and [UserPassword] = ?`)
		if err0 != nil {
			log.Println(err0)
			ui.Info = "SignIn sql db.Prepare error"
		} else {
			defer stmt.Close()
			rows, err := stmt.Query(un, pwd)
			if err != nil {
				fmt.Println("query: ", err)
				ui.Info = "SignIn sql stmt.Query error"
			} else {
				ui.Info = "用户名或密码错误！"
				for rows.Next() {
					rows.Scan(&ui.UserId, &ui.UserName, &ui.UserUuid)
					ui.Info = "登录成功"
					break
				}
			}
		}

	}
	return ui
}

func SignUp(un string, pwd string, cellphone string, email string) {

}

func ModiPassword(un string, pwd string, pwdnew string) (string, int) {
	iresult := 1
	result := ""

	db, err0 := sql.Open("adodb", cfg.Cfg["mssql"])
	if err0 != nil {
		result = "modipassword sql open error"
	} else {
		defer db.Close()
		stmt, err0 := db.Prepare(`SELECT [UserID],[UserName],[userguid] FROM [Dv_User] where [UserName] = ? and [UserPassword] = ?`)
		if err0 != nil {
			result = "modipassword sql db.Prepare error"
		} else {
			defer stmt.Close()
			rows, err := stmt.Query(un, pwd)
			if err != nil {
				result = "modipassword sql stmt.Query error"
			} else {
				result = "您提供的当前密码错误"
				for rows.Next() {
					stmt1, err0 := db.Prepare(`Update [Dv_User] set UserPassword=? where [UserName] = ?`)
					if err0 != nil {
						result = "modipassword sql db.Prepare error"
					} else {
						defer stmt1.Close()
						r1, err := stmt1.Exec(pwdnew, un)
						log.Println(r1)
						if err != nil {
							result = "modipassword sql stmt.Query error"
						} else {
							result = "用户名密码修改成功"
							iresult = 0
						}
					}
					break
				}
			}
		}

	}
	return result, iresult
}
